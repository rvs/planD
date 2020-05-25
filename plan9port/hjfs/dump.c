#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int
copydentry(Fs *fs, FLoc *a, Loc *b, char *nname)
{
	Buf *ba, *bb, *bc;
	Dentry *d;
	int i, rc;
	FLoc c;

	if(!namevalid(nname)){
		werrstr(Einval);
		return -1;
	}
	ba = getbuf(fs->d, a->blk, TDENTRY, 0);
	if(ba == nil)
		return -1;
	bb = getbuf(fs->d, b->FLoc9.blk, TDENTRY, 0);
	if(bb == nil){
		putbuf(ba);
		return -1;
	}
	rc = newentry(fs, b, bb, nname, &c, 1);
	if(rc < 0){
	err1:
		putbuf(bb);
		putbuf(ba);
		return -1;
	}
	bc = getbuf(fs->d, c.blk, TDENTRY, 0);
	if(bc == nil)
		goto err1;
	d = &bc->de[c.deind];
	memcpy(d, &ba->de[a->deind], sizeof(*d));
	strcpy(d->name, nname);
	for(i = 0; i < NDIRECT; i++)
		if(d->db[i] != 0)
			chref(fs, d->db[i], 1);
	for(i = 0; i < NINDIRECT; i++)
		if(d->ib[i] != 0)
			chref(fs, d->ib[i], 1);
	bc->op |= BDELWRI;
	putbuf(bc);
	putbuf(bb);
	putbuf(ba);
	return 0;
}

static void
resetldumped(Fs *fs)
{
	Loc *l;
	
	for(l = fs->rootloc->gnext; l != fs->rootloc; l = l->gnext)
		l->flags &= ~LDUMPED;
}

int
fsdump(Fs *fs)
{
	char buf[20], *p, *e;
	int n, rc;
	Tm *tm;
	Chan *ch, *chh;
	Buf *b;

	wlock(&fs->RWLock9);
	tm = localtime(time(0));
	snprint(buf, sizeof(buf), "%.4d", tm->year + 1900);
	ch = chanattach(fs, CHFNOLOCK|CHFDUMP);
	ch->uid = -1;
	if(ch == nil){
		wunlock(&fs->RWLock9);
		return -1;
	}
	if(chanwalk(ch, buf) < 0){
		chh = chanclone(ch);
		rc = chancreat(chh, buf, DMDIR|0555, OREAD);
		chanclunk(chh);
		if(rc < 0)
			goto err;
		if(chanwalk(ch, buf) < 0)
			goto err;
	}
	b = getbuf(fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil)
		goto err;
	for(n = 0; ; n++){
		e = buf + sizeof(buf);
		p = seprint(buf, e, "%.2d%.2d", tm->mon + 1, tm->mday);
		if(n > 0)
			seprint(p, e, "%d", n);
		rc = findentry(fs, &ch->loc->FLoc9, b, buf, nil, 1);
		if(rc < 0)
			goto err;
		if(rc == 0)
			break;
	}
	putbuf(b);
	rc = copydentry(fs, &fs->rootloc->FLoc9, ch->loc, buf);
	chanclunk(ch);
	resetldumped(fs);
	wunlock(&fs->RWLock9);
	return rc;
err:
	chanclunk(ch);
	wunlock(&fs->RWLock9);
	return -1;
}

static int
willmodify1(Fs *fs, Loc *l)
{
	Buf *p;
	Loc *m;
	uvlong i, r;
	Dentry *d;
	int rc;

	rc = chref(fs, l->FLoc9.blk, 0);
	if(rc < 0)
		return -1;
	if(rc == 0){
		dprint("willmodify: block %lld has refcount 0\n", l->FLoc9.blk);
		werrstr("phase error -- willmodify");
		return -1;
	}
	if(rc == 1)
		goto done;

	p = getbuf(fs->d, l->next->FLoc9.blk, TDENTRY, 0);
	if(p == nil)
		return -1;
	d = getdent(&l->next->FLoc9, p);
	if(d != nil) for(i = 0; i < d->size; i++){
		rc = getblk(fs, &l->next->FLoc9, p, i, &r, GBREAD);
		if(rc <= 0)
			continue;
		if(r == l->FLoc9.blk)
			goto found;
	}	
phase:
	werrstr("willmodify -- phase error");
	putbuf(p);
	return -1;
found:
	rc = getblk(fs, &l->next->FLoc9, p, i, &r, GBWRITE);
	if(rc < 0){
		putbuf(p);
		return -1;
	}
	if(rc == 0)
		goto phase;
	putbuf(p);

	if(r != l->FLoc9.blk){
		/*
		 * block got dumped, update the loctree so locs
		 * point to the new block.
		 */
		qlock(&fs->loctree);
		for(m = l->cnext; m != l; m = m->cnext)
			if(m->FLoc9.blk == l->FLoc9.blk)
				m->FLoc9.blk = r;
		l->FLoc9.blk = r;
		qunlock(&fs->loctree);
	}
done:
	l->flags |= LDUMPED;
	return 0;
}

int
willmodify(Fs *fs, Loc *l, int nolock)
{
	Loc **st;
	int sti, rc;
	
	if((l->flags & LDUMPED) != 0)
		return 1;
	if(!nolock){
again:
		runlock(&fs->RWLock9);
		wlock(&fs->RWLock9);
	}
	st = emalloc(sizeof(Loc *));
	*st = l;
	sti = 0;
	for(;;){
		if((st[sti]->flags & LDUMPED) != 0 || st[sti]->next == nil)
			break;
		st = erealloc(st, (sti + 2) * sizeof(Loc *));
		st[sti + 1] = st[sti]->next;
		sti++;
	}
	rc = 0;
	for(; sti >= 0; sti--){
		rc = willmodify1(fs, st[sti]);
		if(rc < 0){
			free(st);
			if(!nolock){
				wunlock(&fs->RWLock9);
				rlock(&fs->RWLock9);
			}
			return -1;
		}
	}
	if(!nolock){
		wunlock(&fs->RWLock9);
		rlock(&fs->RWLock9);
		if(chref(fs, l->FLoc9.blk, 0) != 1)
			goto again;
	}
	free(st);
	return rc;
}
