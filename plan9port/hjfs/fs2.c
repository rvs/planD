#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

Chan *
chanattach(Fs *fs, int flags)
{	
	Chan *ch;

	ch = emalloc(sizeof(*ch));
	ch->fs = fs;
	ch->flags = flags;
	ch->loc = cloneloc(fs, (flags & CHFDUMP) != 0 ? fs->dumprootloc : fs->rootloc);
	return ch;
}

Chan *
chanclone(Chan *ch)
{
	Chan *d;

	chbegin(ch);
	d = emalloc(sizeof(*d));
	d->fs = ch->fs;
	d->flags = ch->flags;
	d->uid = ch->uid;
	d->loc = cloneloc(ch->fs, ch->loc);
	chend(ch);
	return d;
}

int
chanwalk(Chan *ch, char *name)
{
	Buf *b;
	Dentry *d;
	Loc *l;
	FLoc f;
	
	if(name == nil || name[0] == 0 || name[0] == '.' && name[1] == 0)
		return 1;
	chbegin(ch);
	if(ch->open != 0){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil)
		goto error;
	if((d->Qid9.type & QTDIR) == 0){
		werrstr(Enotadir);
		goto error;
	}
	if(!permcheck(ch->fs, d, ch->uid, OEXEC)){
		werrstr(Eperm);
		goto error;
	}
	if(strcmp(name, "..") == 0){
		l = ch->loc->next;
		if(l == nil)
			goto done;
		putloc(ch->fs, ch->loc, 0);
		ch->loc = l;
		goto done;
	}
	if(findentry(ch->fs, &ch->loc->FLoc9, b, name, &f, ch->flags & CHFDUMP) <= 0)
		goto error;
	ch->loc = getloc(ch->fs, f, ch->loc);
done:
	putbuf(b);
	chend(ch);
	return 1;
error:
	putbuf(b);
	chend(ch);
	return -1;
}

int
namevalid(char *name)
{
	char *p;
	
	if(name == nil || name[0] == 0)
		return 0;
	if(name[0] == '.' && (name[1] == 0 || name[1] == '.' && name[2] == 0))
		return 0;
	for(p = name; *p; p++)
		if((uchar) *p < ' ' || *p == '/')
			return 0;
	return p - name < NAMELEN;
}

int
chancreat(Chan *ch, char *name, int perm, int mode)
{
	Buf *b;
	Dentry *d;
	int isdir;
	Loc *l;
	FLoc f;
	short pgid;

	b = nil;
	l = nil;
	chbegin(ch);
	if(!namevalid(name) || ch->open != 0)
		goto inval;
	if((ch->flags & CHFRO) != 0)
		goto inval;
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0)
		goto error;
	if(isdir = ((perm & DMDIR) != 0))
		if((mode & (OWRITE | OEXEC | ORCLOSE | OTRUNC)) != 0)
			goto inval;
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil)
		goto error;
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil)
		goto error;
	if((d->Qid9.type & QTDIR) == 0){
		werrstr(Enotadir);
		goto error;
	}
	if((ch->flags & CHFNOPERM) == 0){
		if(!permcheck(ch->fs, d, ch->uid, OWRITE)){
			werrstr(Eperm);
			goto error;
		}
		if(isdir)
			perm &= ~0777 | d->mode & 0777;
		else
			perm &= ~0666 | d->mode & 0666;
	}
	if(newentry(ch->fs, ch->loc, b, name, &f, 0) <= 0)
		goto error;

	f.Qid9.type = perm >> 24;
	if(newqid(ch->fs, &f.Qid9.path) < 0)
		goto error;
	l = getloc(ch->fs, f, ch->loc);
	modified(ch, d);
	b->op |= BDELWRI;
	pgid = d->gid;
	putbuf(b);
	b = nil;
	if(willmodify(ch->fs, l, ch->flags & CHFNOLOCK) < 0)
		goto error;
	b = getbuf(ch->fs->d, l->FLoc9.blk, TDENTRY, 0);
	if(b == nil)
		goto error;
	ch->loc = l;
	d = &b->de[l->FLoc9.deind];
	memset(d, 0, sizeof(*d));
	d->Qid9 = l->FLoc9.Qid9;
	strcpy(d->name, name);
	d->mtime = time(0);
	d->atime = d->mtime;
	d->gid = pgid;
	d->uid = d->muid = ch->uid;
	d->mode = DALLOC | perm & 0777;
	if((d->Qid9.type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		ch->loc->exlock = ch;
		ch->loc->lwrite = d->atime;
		qunlock(&ch->loc->ex);
	}
	b->op |= BDELWRI;
	putbuf(b);
	switch(mode & OEXEC){
	case ORDWR:
		ch->open |= CHREAD;
	case OWRITE:
		ch->open |= CHWRITE;
		break;
	case OEXEC:
	case OREAD:
		ch->open |= CHREAD;
		break;
	}
	if((mode & ORCLOSE) != 0)
		ch->open |= CHRCLOSE;
	chend(ch);
	return 1;
inval:
	werrstr(Einval);
error:
	if(l != nil)
		putloc(ch->fs, l, 0);
	if(b != nil)
		putbuf(b);
	chend(ch);
	return -1;
}

int
chanopen(Chan *ch, int mode)
{
	Buf *b;
	Dentry *d;

	b = nil;
	chbegin(ch);
	if(ch->open != 0)
		goto inval;
	if((ch->flags & CHFRO) != 0 && (mode & (ORCLOSE | OTRUNC | OWRITE | ORDWR)) != 0)
		goto inval;
	if((mode & OTRUNC) != 0)
		if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0)
			goto error;
	if((mode & ORCLOSE) != 0){
		if(ch->loc->next == nil)
			goto inval;
		b = getbuf(ch->fs->d, ch->loc->next->FLoc9.blk, TDENTRY, 0);
		if(b == nil)
			goto error;
		d = getdent(&ch->loc->next->FLoc9, b);
		if(d == nil)
			goto error;
		if((ch->flags & CHFNOPERM) == 0)
			if(!permcheck(ch->fs, d, ch->uid, OWRITE))
				goto perm;
		putbuf(b);
	}
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil)
		goto error;
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil)
		goto error;
	if((d->Qid9.type & QTAPPEND) != 0)
		mode &= ~OTRUNC;
	if((d->Qid9.type & QTDIR) != 0 && (mode & (ORCLOSE | OTRUNC | OWRITE | ORDWR)) != 0)
		goto inval;
	if((ch->flags & CHFNOPERM) == 0){
		if(!permcheck(ch->fs, d, ch->uid, mode & OEXEC))
			goto perm;
		if((mode & OTRUNC) != 0 && !permcheck(ch->fs, d, ch->uid, OWRITE))
			goto perm;
	}
	if((ch->loc->FLoc9.Qid9.type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		if(ch->loc->exlock == nil || ch->loc->lwrite < time(0) - EXCLDUR){
			ch->loc->exlock = ch;
			ch->loc->lwrite = time(0);
			qunlock(&ch->loc->ex);
		}else{
			qunlock(&ch->loc->ex);
			werrstr(Elocked);
			goto error;
		}
	}
	switch(mode & OEXEC){
	case ORDWR:
		ch->open |= CHREAD;
	case OWRITE:
		ch->open |= CHWRITE;
		break;
	case OEXEC:
	case OREAD:
		ch->open |= CHREAD;
		break;
	}
	if((mode & OTRUNC) != 0){
		trunc9(ch->fs, &ch->loc->FLoc9, b, 0);
		modified(ch, d);
		b->op |= BDELWRI;
	}
	if((mode & ORCLOSE) != 0)
		ch->open |= CHRCLOSE;
	putbuf(b);
	chend(ch);
	return 1;
inval:
	werrstr(Einval);
	goto error;
perm:
	werrstr(Eperm);
error:
	if(b != nil)
		putbuf(b);
	chend(ch);
	return -1;
}

static int
checklock(Chan *ch)
{
	int rc;

	qlock(&ch->loc->ex);
	rc = 1;
	if(ch->loc->exlock == ch){
		if(ch->loc->lwrite < time(0) - EXCLDUR){
			ch->loc->exlock = nil;
			werrstr("lock broken");
			rc = -1;
		}else
			ch->loc->lwrite = time(0);
	}else{
		werrstr(Elocked);
		rc = -1;
	}
	qunlock(&ch->loc->ex);
	return rc;
}

int
chanwrite(Chan *ch, void *buf, ulong n, uvlong off)
{
	uvlong i, e, bl;
	int r, rn, rc;
	Buf *b, *c;
	Dentry *d;
	uchar *p;

	if(n == 0)
		return 0;
	if((ch->flags & CHFRO) != 0){
		werrstr(Einval);
		return -1;
	}
	if((ch->open & CHWRITE) == 0){
		werrstr(Einval);
		return -1;
	}
	chbegin(ch);
	if((ch->loc->FLoc9.Qid9.type & QTEXCL) != 0 && checklock(ch) < 0){
		chend(ch);
		return -1;
	}
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0){
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil){
		putbuf(b);
		chend(ch);
		return -1;
	}
	if((d->Qid9.type & QTAPPEND) != 0)
		off = d->size;
	e = off + n;
	i = off;
	p = buf;
	while(i < e){
		bl = i / RBLOCK;
		r = i % RBLOCK;
		rn = RBLOCK - r;
		if(i + rn > e)
			rn = e - i;
		rc = getblk(ch->fs, &ch->loc->FLoc9, b, bl, &bl, rn == RBLOCK ? GBOVERWR : GBCREATE);
		if(rc < 0)
			goto done;
		c = getbuf(ch->fs->d, bl, TRAW, rc == 0 || rn == RBLOCK);
		if(c == nil)
			goto done;
		if(rc == 0 && rn != RBLOCK)
			memset(c->data, 0, sizeof(c->data));
		memcpy(c->data + r, p, rn);
		i += rn;
		c->op |= i != e ? BWRIM : BDELWRI;
		putbuf(c);
		p += rn;
	}
done:
	modified(ch, d);
	e = off + (p - (uchar *) buf);
	if(e > d->size)
		d->size = e;
	b->op |= BDELWRI;
	putbuf(b);
	chend(ch);
	if(p == buf)
		return -1;
	return p - (uchar *) buf;
}

static int chandirread(Chan *, void *, ulong, uvlong);

int
chanread(Chan *ch, void *buf, ulong n, uvlong off)
{
	uvlong i, e, bl;
	int r, rn, rc;
	uchar *p;
	Buf *b, *c;
	Dentry *d;

	if((ch->open & CHREAD) == 0){
		werrstr(Einval);
		return -1;
	}
	chbegin(ch);
	if((ch->loc->FLoc9.Qid9.type & QTEXCL) != 0 && checklock(ch) < 0){
		chend(ch);
		return -1;
	}
	if((ch->loc->FLoc9.Qid9.type & QTDIR) != 0)
		return chandirread(ch, buf, n, off);
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil)
		goto error;
	if(off >= d->size)
		n = 0;
	else if(off + n > d->size)
		n = d->size - off;
	if(n == 0){
		putbuf(b);
		chend(ch);
		return 0;
	}
	e = off + n;
	i = off;
	p = buf;
	while(i < e){
		bl = i / RBLOCK;
		r = i % RBLOCK;
		rn = RBLOCK - r;
		if(i + rn > e)
			rn = e - i;
		rc = getblk(ch->fs, &ch->loc->FLoc9, b, bl, &bl, GBREAD);
		if(rc < 0)
			goto error;
		if(rc == 0)
			memset(p, 0, rn);
		else{
			c = getbuf(ch->fs->d, bl, TRAW, 0);
			if(c == nil)
				goto error;
			memcpy(p, c->data + r, rn);
			putbuf(c);
		}
		i += rn;
		p += rn;
	}
	putbuf(b);
	chend(ch);
	return n;
error:
	putbuf(b);
	chend(ch);
	return -1;
}

static void
statbuf(Fs *fs, Dentry *d, Dir *di, char *buf)
{
	di->qid = d->Qid9;
	di->mode = (d->mode & 0777) | (d->Qid9.type << 24);
	di->mtime = d->mtime;
	di->atime = d->atime;
	di->length = d->size;
	if(d->Qid9.type & QTDIR)
		di->length = 0;
	if(buf == nil){
		di->name = estrdup(d->name);
		di->uid = uid2name(fs, d->uid, nil);
		di->gid = uid2name(fs, d->gid, nil);
		di->muid = uid2name(fs, d->muid, nil);
	}else{
		memset(buf, 0, NAMELEN + 3 * USERLEN);
		strncpy(buf, d->name, NAMELEN - 1);
		di->name = buf;
		di->uid = uid2name(fs, d->uid, buf + NAMELEN);
		di->gid = uid2name(fs, d->gid, buf + NAMELEN + USERLEN);
		di->muid = uid2name(fs, d->muid, buf + NAMELEN + 2 * USERLEN);
	}
}

int
chanstat(Chan *ch, Dir *di)
{
	Buf *b;
	Dentry *d;
	
	chbegin(ch);
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil){
		putbuf(b);
		chend(ch);
		return -1;
	}
	statbuf(ch->fs, d, di, nil);
	putbuf(b);
	chend(ch);
	return 0;
}

static int
chandirread(Chan *ch, void *buf, ulong n, uvlong off)
{
	Buf *b, *c;
	Dentry *d;
	uvlong i, blk;
	int j;
	int rc;
	ulong wr;
	Dir di;
	char cbuf[NAMELEN + 3 * USERLEN];

	if(off == 0){
		ch->dwloff = 0;
		ch->dwblk = 0;
		ch->dwind = 0;
	}else if(ch->dwloff != off){
		werrstr(Einval);
		chend(ch);
		return -1;
	}
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		chend(ch);
		return -1;
	}
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil){
		putbuf(b);
		chend(ch);
		return -1;
	}
	if(ch->dwblk >= d->size){
		putbuf(b);
		chend(ch);
		return 0;
	}
	c = nil;
	wr = 0;
	i = ch->dwblk;
	j = ch->dwind;
	for(;;){
		if(c == nil){
			rc = getblk(ch->fs, &ch->loc->FLoc9, b, i, &blk, GBREAD);
			if(rc < 0)
				goto error;
			if(rc == 0){
				j = 0;
				if(++i >= d->size)
					break;
				continue;
			}
			c = getbuf(ch->fs->d, blk, TDENTRY, 0);
			if(c == nil)
				goto error;
		}
		if((c->de[j].mode & DALLOC) == 0)
			goto next;
		if((ch->flags & CHFDUMP) != 0 && (c->de[j].Qid9.type & QTTMP) != 0)
			goto next;
		statbuf(ch->fs, &c->de[j], &di, cbuf);
		rc = convD2M(&di, (uchar *) buf + wr, n - wr);
		if(rc <= BIT16SZ)
			break;
		wr += rc;
	next:
		if(++j >= DEPERBLK){
			j = 0;
			if(c != nil)
				putbuf(c);
			c = nil;
			if(++i >= d->size)
				break;
		}
	}
	ch->dwblk = i;
	ch->dwind = j;
	ch->dwloff += wr;
	if(c != nil)
		putbuf(c);
	putbuf(b);
	chend(ch);
	return wr;
error:
	putbuf(b);
	chend(ch);
	return -1;
}

int
chanclunk(Chan *ch)
{
	Buf *b, *p;
	int rc;
	Dentry *d;

	rc = 1;
	b = p = nil;
	chbegin(ch);
	if(ch->open & CHRCLOSE){
		if((ch->flags & CHFRO) != 0)
			goto inval;
		if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0)
			goto error;
		if(ch->loc->next == nil)
			goto inval;
		p = getbuf(ch->fs->d, ch->loc->next->FLoc9.blk, TDENTRY, 0);
		if(p == nil)
			goto error;
		b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
		if(b == nil)
			goto error;
		d = getdent(&ch->loc->next->FLoc9, p);
		if(d == nil)
			goto error;
		if((ch->flags & CHFNOPERM) == 0)
			if(!permcheck(ch->fs, d, ch->uid, OWRITE)){
				werrstr(Eperm);
				goto error;
			}
		d = getdent(&ch->loc->FLoc9, b);
		if(d == nil)
			goto error;
		if((d->Qid9.type & QTDIR) != 0 && findentry(ch->fs, &ch->loc->FLoc9, b, nil, nil, ch->flags & CHFDUMP) != 0)
			goto inval;
		if((d->mode & DGONE) != 0)
			goto done;
		qlock(&ch->fs->loctree);
		if(ch->loc->ref > 1){
			d->mode &= ~DALLOC;
			d->mode |= DGONE; /* aaaaand it's gone */
			ch->loc->flags |= LGONE;
			qunlock(&ch->fs->loctree);
		}else{
			ch->loc->flags &= ~LGONE;
			qunlock(&ch->fs->loctree);
			rc = delete(ch->fs, &ch->loc->FLoc9, b);
		}
		b->op |= BDELWRI;
	}
done:
	if(b != nil)
		putbuf(b);
	if(p != nil)
		putbuf(p);
	if((ch->loc->FLoc9.Qid9.type & QTEXCL) != 0){
		qlock(&ch->loc->ex);
		if(ch->loc->exlock == ch)
			ch->loc->exlock = nil;
		qunlock(&ch->loc->ex);
	}
	putloc(ch->fs, ch->loc, 1);
	chend(ch);
	free(ch);
	return rc;
inval:
	werrstr(Einval);
error:
	rc = -1;
	goto done;
}

int
chanwstat(Chan *ch, Dir *di)
{
	Buf *b, *pb;
	Dentry *d;
	int isdir, owner, rc;
	short nuid, ngid;

	b = pb = nil;
	chbegin(ch);
	if((ch->flags & CHFRO) != 0)
		goto inval;
	if(willmodify(ch->fs, ch->loc, ch->flags & CHFNOLOCK) < 0)
		goto error;
	if(*di->name){
		FLoc f;

		if(!namevalid(di->name) || ch->loc->next == nil)
			goto inval;
		pb = getbuf(ch->fs->d, ch->loc->next->FLoc9.blk, TDENTRY, 0);
		if(pb == nil)
			goto error;
		d = getdent(&ch->loc->next->FLoc9, pb);
		if(d == nil)
			goto error;
		rc = findentry(ch->fs, &ch->loc->next->FLoc9, pb, di->name, &f, ch->flags & CHFDUMP);
		if(rc < 0)
			goto error;
		else if(rc == 0){
			if((ch->flags & CHFNOPERM) == 0)
				if(!permcheck(ch->fs, d, ch->uid, OWRITE))
					goto perm;
		} else if(f.blk != ch->loc->FLoc9.blk || f.deind != ch->loc->FLoc9.deind){
			werrstr(Eexists);
			goto error;
		}
	}
	b = getbuf(ch->fs->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil)
		goto error;
	d = getdent(&ch->loc->FLoc9, b);
	if(d == nil)
		goto error;
	isdir = (d->Qid9.type & QTDIR) != 0;
	owner = ch->uid == d->uid ||
		ingroup(ch->fs, ch->uid, d->gid, 1) ||
		(ch->fs->flags & FSNOPERM) != 0 ||
		(ch->flags & CHFNOPERM) != 0;
	if(di->length != ~0){
		if(isdir && di->length != 0)
			goto inval;
		if((ch->flags & CHFNOPERM) == 0)
			if(di->length != d->size && !permcheck(ch->fs, d, ch->uid, OWRITE))
				goto perm;
	}
	if(di->mtime != ~0 && !owner)
		goto perm;
	if(di->mode != ~0 && !owner)
		goto perm;
	nuid = d->uid;
	ngid = d->gid;
	if(*di->uid != 0 && name2uid(ch->fs, di->uid, &nuid) < 0)
		goto inval;
	if(*di->gid != 0 && name2uid(ch->fs, di->gid, &ngid) < 0)
		goto inval;
	if(nuid != d->uid && (ch->fs->flags & FSCHOWN) == 0)
		goto perm;
	if((nuid != d->uid || ngid != d->gid) && !owner)
		goto perm;
	d->uid = nuid;
	d->gid = ngid;
	if(di->length != ~0 && di->length != d->size && !isdir){
		trunc9(ch->fs, &ch->loc->FLoc9, b, di->length);
		modified(ch, d);
	}
	if(di->mtime != ~0)
		d->mtime = di->mtime;
	if(di->mode != ~0){
		d->mode = d->mode & ~0777 | di->mode & 0777;
		ch->loc->FLoc9.Qid9.type = d->Qid9.type = di->mode >> 24;
	}
	if(*di->name){
		memset(d->name, 0, NAMELEN);
		strcpy(d->name, di->name);
	}
	b->op |= BDELWRI;
	if(pb != nil)
		putbuf(pb);
	putbuf(b);
	chend(ch);
	return 1;
inval:
	werrstr(Einval);
	goto error;
perm:
	werrstr(Eperm);
error:
	if(pb != nil)
		putbuf(pb);
	if(b != nil)
		putbuf(b);
	chend(ch);
	return -1;
}

int
chanremove(Chan *ch)
{
	ch->open |= CHRCLOSE;
	return chanclunk(ch);
}
