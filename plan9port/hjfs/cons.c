#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"

enum { MAXARGS = 16 };

typedef struct Cmd Cmd;
static int echo;
extern Fs *fsmain;

struct Cmd {
	char *name;
	int args;
	int (*f)(int, char **);
};

static int
walkpath(Chan *ch, char *path, char **cr)
{
	char buf[NAMELEN], *p, *fp;

	fp = path;
	if(*path != '/'){
	noent:
		werrstr("%s: %s", fp, Enoent);
		return -1;
	}
	path++;
	for(;;){
		p = strchr(path, '/');
		if(p == nil){
			if(cr != nil){
				if(*path == 0){
					werrstr("%s: trailing slash", fp);
					return -1;
				}
				*cr = path;
				break;
			}
			p = path + strlen(path);
		}
		if(*path == '/'){
			path++;
			continue;
		}
		if(*path == 0)
			break;
		if(p - path >= NAMELEN)
			goto noent;
		memset(buf, 0, sizeof buf);
		memcpy(buf, path, p - path);
		if(chanwalk(ch, buf) <= 0){
			werrstr("%s: %r", fp);
			return -1;
		}
		if(*p == 0)
			break;
		path = p + 1;
	}
	return 1;
}

int
cmdsync(int dummy1, char **dummy2)
{
	sync9(1);
	dprint("sync\n");
	return 0;
}

int
cmdhalt(int dummy1, char ** dummy2)
{
	shutdown();
	return 0;
}

int
cmddump(int dummy1, char **dummy2)
{
	fsdump(fsmain);
	dprint("dumped\n");
	return 0;
}

int
cmdallow(int dummy1, char **dummy2)
{
	fsmain->flags |= FSNOPERM | FSCHOWN;
	dprint("allow\n");
	return 0;
}

int
cmdchatty(int dummy1, char **dummy2)
{
	extern int chatty9p;

	chatty9p = !chatty9p;
	return 0;
}

static void
checkfile(FLoc *l, Buf *b)
{
	Buf *c;
	Dentry *d;
	char *ftype;
	int btype, rc;
	uvlong i, r, blocks;

	d = getdent(l, b);
	if(d == nil){
		dprint("checkfile: bad entry: %r\n");
		return;
	}
	if((d->Qid9.type & QTDIR) == 0){
		ftype = "file";
		btype = TRAW;
		blocks = HOWMANY(d->size);
	}else{
		ftype = "directory";
		btype = TDENTRY;
		blocks = d->size;
	}

	for(i = 0; i < blocks; i++){
		rc = getblk(fsmain, l, b, i, &r, GBREAD);
		if(rc < 0){
			dprint("bad block %ulld of %ulld in %s %s with directory entry index %d in block %ulld: %r\n", i, blocks, ftype, d->name, l->deind, l->blk);
			continue;
		}
		if(rc == 0){
			dprint("block %ulld of %ulld not found in %s %s with directory index %d in block %ulld\n", i, blocks, ftype, d->name, l->deind, l->blk);
			continue;
		}
		c = getbuf(fsmain->d, r, btype, 0);
		if(c == nil)
			dprint("bad block %ulld of %ulld in %s %s with directory entry index %d in block %ulld: %r\n", i, blocks, ftype, d->name, l->deind, l->blk);
		putbuf(c);
		if(chref(fsmain, r, 0) == 0)
			dprint("block %ulld of %ulld in %s %s with directory entry index %d in block %ulld has a reference count of 0", i, blocks, ftype, d->name, l->deind, l->blk);
	}
}

static int
checkblk(uvlong blk)
{
	Dentry *d;
	Buf *b;
	FLoc l;
	int i, type;

	b = getbuf(fsmain->d, blk, TDONTCARE, 0);
	if(b == nil)
		return -1;
	switch(type = b->type){
	case TSUPERBLOCK:
		if(blk != SUPERBLK)
			dprint("checkblk: should not have found superblock at %ulld\n", blk);
		break;
	case TDENTRY:
		l.blk = blk;
		for(i = 0; i < DEPERBLK; i++){
			d = &b->de[i];
			if((d->mode & (DGONE | DALLOC)) == 0)
				break;
			l.deind = i;
			l.Qid9 = d->Qid9;
			checkfile(&l, b);
		}
		break;
	}
	putbuf(b);
	return type;
}

int
cmdcheck(int dummy1, char** dummy2)
{
	static ulong refs[REFPERBLK];
	uvlong fblk, fend, blk;
	uvlong ndentry, nindir, nraw, nref, nsuperblock;
	int j;
	Buf *b, *sb;

	wlock(&fsmain->RWLock9);
	sb = getbuf(fsmain->d, SUPERBLK, TSUPERBLOCK, 0);
	if(sb == nil){
		wunlock(&fsmain->RWLock9);
		return -1;
	}
	fblk = sb->sb.fstart;
	fend = sb->sb.fend;
	putbuf(sb);

	ndentry = 0;
	nindir = 0;
	nraw = 0;
	nref = 0;
	nsuperblock = 0;
	for(blk = 0; fblk < fend; fblk++){
		b = getbuf(fsmain->d, fblk, TREF, 0);
		if(b == nil){
			blk += REFPERBLK;
			continue;
		}
		memcpy(refs, b->refs, sizeof(refs));
		putbuf(b);
		for(j = 0; j < REFPERBLK; j++, blk++){
			if(refs[j] > 0 && refs[j] != REFSENTINEL){
				switch(checkblk(blk)){
				case TDENTRY:
					ndentry++;
					break;
				case TINDIR:
					nindir++;
					break;
				case TRAW:
					nraw++;
					break;
				case TREF:
					nref++;
					break;
				case TSUPERBLOCK:
					nsuperblock++;
					break;
				}
			}
		}
	}
	wunlock(&fsmain->RWLock9);
	dprint("%T block count %ulld\n", TDENTRY, ndentry);
	dprint("%T block count %ulld\n", TINDIR, nindir);
	dprint("%T block count %ulld\n", TRAW, nraw);
	dprint("%T block count %ulld\n", TREF, nref);
	dprint("%T block count %ulld\n", TSUPERBLOCK, nsuperblock);
	return 1;
}

int
cmddisallow(int dummy1, char **dummy2)
{
	fsmain->flags &= ~(FSNOPERM | FSCHOWN);
	dprint("disallow\n");
	return 0;
}

int
cmdnoauth(int dummy1, char **dummy2)
{
	fsmain->flags ^= FSNOAUTH;
	if((fsmain->flags & FSNOAUTH) == 0)
		dprint("auth enabled\n");
	else
		dprint("auth disabled\n");
	return 1;
}

int
cmdcreate(int argc, char **argv)
{
	Chan *ch;
	char *n;
	short uid, gid;
	ulong perm;
	Dir di;

	if(argc != 5 && argc != 6)
		return -9001;
	perm = strtol(argv[4], &n, 8) & 0777;
	if(*n != 0)
		return -9001;
	if(argc == 6)
		for(n = argv[5]; *n != 0; n++)
			switch(*n){
			case 'l': perm |= DMEXCL; break;
			case 'd': perm |= DMDIR; break;
			case 'a': perm |= DMAPPEND; break;
			default: return -9001;
			}
	if(name2uid(fsmain, argv[2], &uid) < 0)
		return -1;
	if(name2uid(fsmain, argv[3], &gid) < 0)
		return -1;
	ch = chanattach(fsmain, CHFNOPERM);
	if(ch == nil)
		return -1;
	ch->uid = uid;
	if(walkpath(ch, argv[1], &n) < 0){
		chanclunk(ch);
		return -1;
	}
	if(chancreat(ch, n, perm, OREAD) < 0){
		chanclunk(ch);
		return -1;
	}
	nulldir(&di);
	di.gid = argv[3];
	chanwstat(ch, &di);
	chanclunk(ch);
	return 1;
}

int
cmdecho(int dummy, char **argv)
{
	echo = strcmp(argv[1], "on") == 0;
	return 1;
}

int
cmddf(int dummy1, char **dummy2)
{
	uvlong n;
	uvlong i;
	int j;
	Buf *b, *sb;

	wlock(&fsmain->RWLock9);
	sb = getbuf(fsmain->d, SUPERBLK, TSUPERBLOCK, 0);
	if(sb == nil){
		wunlock(&fsmain->RWLock9);
		return -1;
	}
	n = 0;
	for(i = sb->sb.fstart; i < sb->sb.fend; i++){
		b = getbuf(fsmain->d, i, TREF, 0);
		if(b == nil)
			continue;
		for(j = 0; j < REFPERBLK; j++)
			if(b->refs[j] == 0)
				n++;
		putbuf(b);
	}
	dprint("(blocks) free %ulld, used %ulld, total %ulld\n", n, sb->sb.size - n, sb->sb.size);
	dprint("(MB) free %ulld, used %ulld, total %ulld\n", n * BLOCK / 1048576, (sb->sb.size - n) * BLOCK / 1048576, sb->sb.size * BLOCK / 1048576);
	putbuf(sb);
	wunlock(&fsmain->RWLock9);
	return 1;
}

int
cmddebugdeind(int dummy, char **argv)
{
	Chan *ch;
	Buf *b;
	Dentry *d;

	ch = chanattach(fsmain, 0);
	if(ch == nil)
		return -1;
	ch->uid = -1;
	if(walkpath(ch, argv[1], nil) < 0)
		goto error;
	rlock(&fsmain->RWLock9);
	dprint("loc %ulld / %uld, offset %ulld\n", ch->loc->FLoc9.blk, ch->loc->FLoc9.deind, BLOCK * ch->loc->FLoc9.blk + (RBLOCK - BLOCK) + DENTRYSIZ * ch->loc->FLoc9.deind);
	b = getbuf(fsmain->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		runlock(&fsmain->RWLock9);
		goto error;
	}
	d = &b->de[ch->loc->FLoc9.deind];
	dprint("name %s\n", d->name);
	dprint("uid %d, muid %d, gid %d\n", d->uid, d->muid, d->gid);
	dprint("mode %#o, qid %ulld, type %#x, version %d\n", d->mode, d->Qid9.path, d->Qid9.type, d->Qid9.vers);
	dprint("size %d\n", d->size);
	dprint("atime %ulld, mtime %ulld\n", d->atime, d->mtime);
	putbuf(b);
	runlock(&fsmain->RWLock9);
	chanclunk(ch);
	return 0;
error:
	chanclunk(ch);
	return -1;
}

int
cmddebugchdeind(int dummy, char **argv)
{
	Chan *ch;
	uchar *c;
	Buf *b;
	int loc, new;

	loc = strtol(argv[2], nil, 0);
	new = strtol(argv[3], nil, 0);
	if(loc >= DENTRYSIZ)
		return -9001;
	ch = chanattach(fsmain, 0);
	if(ch == nil)
		return -1;
	ch->uid = -1;
	if(walkpath(ch, argv[1], nil) < 0)
		goto error;
	rlock(&fsmain->RWLock9);
	b = getbuf(fsmain->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		runlock(&fsmain->RWLock9);
		goto error;
	}
	c = (uchar *) &b->de[ch->loc->FLoc9.deind];
	dprint("loc %d, old value %#.2x, new value %#.2x\n", loc, c[loc], new);
	c[loc] = new;
	b->op |= BDELWRI;
	putbuf(b);
	runlock(&fsmain->RWLock9);
	chanclunk(ch);
	return 0;
error:
	chanclunk(ch);
	return -1;
}

int
cmddebuggetblk(int argc, char **argv)
{
	Chan *ch;
	Buf *b;
	int rc;
	uvlong r, start, end, i;

	if(argc != 3 && argc != 4)
		return -9001;
	start = atoll(argv[2]);
	if(argc == 4)
		end = atoll(argv[3]);
	else
		end = start;
	ch = chanattach(fsmain, 0);
	if(ch == nil)
		return -1;
	ch->uid = -1;
	if(walkpath(ch, argv[1], nil) < 0)
		goto error;
	rlock(&fsmain->RWLock9);
	b = getbuf(fsmain->d, ch->loc->FLoc9.blk, TDENTRY, 0);
	if(b == nil){
		runlock(&fsmain->RWLock9);
		goto error;
	}
	for(i = start; i <= end; i++){
		rc = getblk(fsmain, &ch->loc->FLoc9, b, i, &r, GBREAD);
		if(rc > 0)
			dprint("getblk %ulld = %ulld\n", i, r);
		if(rc == 0)
			dprint("getblk %ulld not found\n", i);
		if(rc < 0)
			dprint("getblk %ulld: %r\n", i);
	}
	putbuf(b);
	runlock(&fsmain->RWLock9);
	chanclunk(ch);
	return 0;
error:
	chanclunk(ch);
	return -1;
}

int
cmdusers(int dummy1, char** dummy2)
{
	readusers(fsmain);
	return 0;
}

extern int cmdnewuser(int, char **);

Cmd cmds[] = {
	{"allow", 1, cmdallow},
	{"noauth", 1, cmdnoauth},
	{"chatty", 1, cmdchatty},
//	{"check", 0, cmdcheck},
	{"create", 0, cmdcreate},
	{"disallow", 1, cmddisallow},
	{"dump", 1, cmddump},
	{"sync", 1, cmdsync},
	{"halt", 1, cmdhalt},
	{"newuser", 0, cmdnewuser},
	{"users", 1, cmdusers},
	{"echo", 2, cmdecho},
	{"df", 1, cmddf},
	{"debug-deind", 2, cmddebugdeind},
	{"debug-getblk", 0, cmddebuggetblk},
	{"debug-chdeind", 4, cmddebugchdeind},
};


static void
consproc(void *v)
{
	Biobuf *in;
	Cmd *c;
	char *s;
	char *args[MAXARGS];
	int rc;
	
	in = (Biobuf *) v;
	for(;;){
		s = Brdstr(in, '\n', 1);
		if(s == nil)
			continue;
		if(echo)
			dprint(">%s\n", s);
		rc = tokenize(s, args, MAXARGS);
		if(rc == 0)
			goto syntax;
		for(c = cmds; c < cmds + nelem(cmds); c++)
			if(strcmp(c->name, args[0]) == 0){
				if(c->args != 0 && c->args != rc)
					goto syntax;
				if(c->f != nil){
					rc = c->f(rc, args);
					if(rc == -9001)
						goto syntax;
					if(rc < 0)
						dprint("%r\n");
					goto done;
				}
			}
	syntax:
		dprint("syntax error\n");
	done:
		free(s);
	}
}

void
initcons(char *service)
{
	int fd, pfd[2];
	static Biobuf bio;
	char buf[512];

	snprint(buf, sizeof(buf), "/srv/%s.cmd", service);
	fd = create(buf, OWRITE|ORCLOSE, 0600);
	if(fd < 0)
		return;
	pipe(pfd);
	fprint(fd, "%d", pfd[1]);
	Binit(&bio, pfd[0], OREAD);
	proccreate(consproc, &bio, mainstacksize);
}
