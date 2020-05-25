#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include "dat.h"
#include "fns.h"
#include "auth.h"

extern Fs *fsmain;


typedef struct Afid Afid;

struct Afid
{
        AuthRpc *rpc;
        char *uname;
        char *aname;
        int authok;
        int afd;
};

extern void respond(Req *, char *);
static void responderror(Req *r)
{
        char errbuf[ERRMAX];

        rerrstr(errbuf, sizeof errbuf);
        respond(r, errbuf);
}

static void authdestroy(Fid *fid)
{
        Afid *afid;

        if((fid->qid.type & QTAUTH) && (afid = fid->aux) != nil){
                if(afid->rpc)
                        auth_freerpc(afid->rpc);
                close(afid->afd);
                free(afid->uname);
                free(afid->aname);
                free(afid);
                fid->aux = nil;
        }
}

static int _authread(Afid *afid, void *data, int count)
{
        AuthInfo *ai;

        switch(auth_rpc(afid->rpc, "read", nil, 0)){
        case ARdone:
                ai = auth_getinfo(afid->rpc);
                if(ai == nil)
                        return -1;
                auth_freeAI(ai);
                if(chatty9p)
                        fprint(2, "authenticate %s/%s: ok\n", afid->uname, afid->aname);
                afid->authok = 1;
                return 0;

        case ARok:
                if(count < afid->rpc->narg){
                        werrstr("authread count too small");
                        return -1;
                }
                count = afid->rpc->narg;
                memmove(data, afid->rpc->arg, count);
                return count;

        case ARphase:
        default:
                werrstr("authrpc botch");
                return -1;
        }
}

static void authread(Req *r)
{
        int n;
        Afid *afid;
        Fid *fid;

        fid = r->fid;
        afid = fid->aux;
        if(afid == nil || r->fid->qid.type != QTAUTH){
                respond(r, "not an auth fid");
                return;
        }
        n = _authread(afid, r->ofcall.data, r->ifcall.count);
        if(n < 0){
                responderror(r);
                return;
        }
        r->ofcall.count = n;
        respond(r, nil);
}

static void authwrite(Req *r)
{
        Afid *afid;
        Fid *fid;

        fid = r->fid;
        afid = fid->aux;
        if(afid == nil || r->fid->qid.type != QTAUTH){
                respond(r, "not an auth fid");
                return;
        }
        if(auth_rpc(afid->rpc, "write", r->ifcall.data, r->ifcall.count) != ARok){
                responderror(r);
                return;
        }
        r->ofcall.count = r->ifcall.count;
        respond(r, nil);
}

static int authattach(Req *r)
{
        Afid *afid;
        char buf[ERRMAX];

        if(r->afid == nil){
                respond(r, "not authenticated");
                return -1;
        }

        afid = r->afid->aux;
        if((r->afid->qid.type&QTAUTH) == 0 || afid == nil){
                respond(r, "not an auth fid");
                return -1;
        }

        if(!afid->authok){
                if(_authread(afid, buf, 0) < 0){
                        responderror(r);
                        return -1;
                }
        }

        if(strcmp(afid->uname, r->ifcall.uname) != 0){
                snprint(buf, sizeof buf, "auth uname mismatch: %s vs %s",
                        afid->uname, r->ifcall.uname);
                respond(r, buf);
                return -1;
        }

        if(strcmp(afid->aname, r->ifcall.aname) != 0){
                snprint(buf, sizeof buf, "auth aname mismatch: %s vs %s",
                        afid->aname, r->ifcall.aname);
                respond(r, buf);
                return -1;
        }
        return 0;
}

static uvlong authgen = 1ULL<<63;
static void auth9p(Req *r)
{
        char *spec;
        Afid *afid;

        afid = emalloc9p(sizeof(Afid));
        afid->afd = open("/mnt/factotum/rpc", ORDWR);
        if(afid->afd < 0)
                goto error;

       if((afid->rpc = auth_allocrpc()) == nil) /* FIXME: auth_allocrpc(afid->afd) */
                goto error;

        if(r->ifcall.uname[0] == 0)
                goto error;
        afid->uname = estrdup9p(r->ifcall.uname);
        afid->aname = estrdup9p(r->ifcall.aname);

        /* spec = r->srv->keyspec; FIXME */
        if(spec == nil)
                spec = "proto=p9any role=server";

        if(auth_rpc(afid->rpc, "start", spec, strlen(spec)) != ARok)
                goto error;

        r->afid->qid.type = QTAUTH;
        r->afid->qid.path = ++authgen;
        r->afid->qid.vers = 0;
        r->afid->omode = ORDWR;
        r->ofcall.qid = r->afid->qid;
        r->afid->aux = afid;
        respond(r, nil);
        return;

error:
        if(afid->rpc)
                auth_freerpc(afid->rpc);
        if(afid->uname)
                free(afid->uname);
        if(afid->aname)
                free(afid->aname);
        if(afid->afd >= 0)
                close(afid->afd);
        free(afid);
        responderror(r);
}

static char *srvaddr;
static void (*_forker)(void(*)(void*), void*, int);

static void
srvproc(void *v)
{
        srv((Srv*)v);
}

static void listenproc(void *v)
{
        char ndir[NETPATHLEN], dir[NETPATHLEN];
        int ctl, data, nctl;
        Srv *os, *s;

        os = v;
        ctl = announce(srvaddr, dir);
        if(ctl < 0){
                fprint(2, "%s: announce %s: %r", argv0, srvaddr);
                return;
        }

        for(;;){
                nctl = listen(dir, ndir);
                if(nctl < 0){
                        fprint(2, "%s: listen %s: %r", argv0, srvaddr);
                        break;
                }

                data = accept(nctl, ndir);
                if(data < 0){
                        fprint(2, "%s: accept %s: %r\n", argv0, ndir);
                        close(nctl);
                        continue;
                }
                close(nctl);

                s = emalloc9p(sizeof *s);
                *s = *os;
                s->infd = s->outfd = data;
                s->fpool = nil;
                s->rpool = nil;
                s->rbuf = nil;
                s->wbuf = nil;
                /* s->free = srvfree; FIXME */
                _forker(srvproc, s, 0);
        }
        free(os);
}

static void _listensrv(Srv *os, char *addr)
{
        Srv *s;

        if(_forker == nil)
                sysfatal("no forker");
        s = emalloc9p(sizeof *s);
        *s = *os;
	srvaddr = estrdup9p(addr); /* FIXME: s->addr = estrdup9p(addr); */
        _forker(listenproc, s, 0);
}

static void tforker(void (*fn)(void*), void *arg, int rflag)
{
	proccreate(fn, arg, 32*1024); /* FIXME: procrfork(fn, arg, 32*1024, rflag); */
}

static void threadlistensrv(Srv *s, char *addr)
{
        _forker = tforker;
        _listensrv(s, addr);
}


static void
tauth(Req *req)
{
	if((fsmain->flags & FSNOAUTH) != 0)
		respond(req, "no authentication required");
	else
		auth9p(req);
}

static void
tattach(Req *req)
{
	Chan *ch;
	int flags;
	short uid;

	if((fsmain->flags & FSNOAUTH) == 0 && authattach(req) < 0)
		return;
	if(name2uid(fsmain, req->ifcall.uname, &uid) <= 0){
		fprint(2, "user: %s\n", req->ifcall.uname);
		respond(req, "no such user");
		return;
	}
	if(req->ifcall.aname == nil || *req->ifcall.aname == 0)
		flags = 0;
	else if(strcmp(req->ifcall.aname, "dump") == 0)
		flags = CHFDUMP|CHFRO;
	else{
		respond(req, Einval);
		return;
	}
	ch = chanattach(fsmain, flags);
	ch->uid = uid;
	req->fid->aux = ch;
	req->fid->qid = ch->loc->FLoc9.Qid9;
	req->ofcall.qid = ch->loc->FLoc9.Qid9;
	respond(req, nil);
}

static void
tqueue(Req *req)
{
	Chan *ch;

	if((req->fid->qid.type & QTAUTH) != 0){
		switch(req->ifcall.type){
		case Tread:
			authread(req);
			return;
		case Twrite:
			authwrite(req);
			return;
		default:
			respond(req, Einval);
			return;
		}
	}
	ch = req->fid->aux;
	if(ch == nil){
		respond(req, "operation on closed fid");
		return;
	}
	qlock(&chanqu);
	req->aux = nil;
	if(ch->freq == nil)
		ch->freq = req;
	if(ch->lreq != nil)
		((Req *) ch->lreq)->aux = req;
	ch->lreq = req;
	if(ch->qnext == nil && (ch->wflags & CHWBUSY) == 0){
		ch->qnext = &readych;
		ch->qprev = ch->qnext->qprev;
		ch->qnext->qprev = ch;
		ch->qprev->qnext = ch;
		rwakeup(&chanre);
	}
	if(req->ifcall.type == Tremove)
		req->fid->aux = nil;
	qunlock(&chanqu);
}

static void
tdestroyfid(Fid *fid)
{
	Chan *ch;

	if((fid->qid.type & QTAUTH) != 0){
		authdestroy(fid);
		return;
	}
	qlock(&chanqu);
	ch = fid->aux;
	fid->aux = nil;
	if(ch != nil){
		ch->wflags |= CHWCLUNK;
		if(ch->qnext == nil && (ch->wflags & CHWBUSY) == 0){
			ch->qnext = &readych;
			ch->qprev = ch->qnext->qprev;
			ch->qnext->qprev = ch;
			ch->qprev->qnext = ch;
			rwakeup(&chanre);
		}
	}
	qunlock(&chanqu);
}

static void
tend(Srv *dummy)
{
	shutdown();
}

static Srv mysrv = {
	.auth = tauth,
	.attach = tattach,
	.walk = tqueue,
	.open = tqueue,
	.create = tqueue,
	.read = tqueue,
	.write = tqueue,
	.stat = tqueue,
	.wstat = tqueue,
	.remove = tqueue,
	.destroyfid = tdestroyfid,
	.end = tend,
};

void
start9p(char *service, char **nets, int stdio)
{
	while(nets && *nets){
		mysrv.end = nil;	/* disable shutdown */
		threadlistensrv(&mysrv, *nets++);
	}
	if(stdio){
		mysrv.infd = 1;
		mysrv.outfd = 1;
		srv(&mysrv);
	}else
		threadpostmountsrv(&mysrv, service, nil, 0);
}

static int
twalk(Chan *ch, Fid *fid, Fid *nfid, int n, char **name, Qid *qid)
{
	int i;

	if(nfid != fid){
		if(ch->open != 0){
			werrstr("trying to clone an open fid");
			return -1;
		}
		ch = chanclone(ch);
		if(ch == nil)
			return -1;
		nfid->aux = ch;
		nfid->qid = ch->loc->FLoc9.Qid9;
	}
	for(i = 0; i < n; i++){
		if(chanwalk(ch, name[i]) <= 0)
			return i > 0 ? i : -1;
		qid[i] = ch->loc->FLoc9.Qid9;
		nfid->qid = ch->loc->FLoc9.Qid9;
	}
	return n;
}

static void
workerproc(void *dummy)
{
	Chan *ch;
	Req *req;
	int rc;
	Fcall *i, *o;

	qlock(&chanqu);
	for(;;){
		while(readych.qnext == &readych)
			rsleep(&chanre);
		ch = readych.qnext;
		ch->qnext->qprev = ch->qprev;
		ch->qprev->qnext = ch->qnext;
		ch->qprev = nil;
		ch->qnext = nil;
		assert((ch->wflags & CHWBUSY) == 0);
		ch->wflags |= CHWBUSY;
		while(ch != nil && ch->freq != nil){
			req = ch->freq;
			ch->freq = req->aux;
			if(ch->lreq == req)
				ch->lreq = nil;
			req->aux = nil;
			qunlock(&chanqu);
			assert(req->responded == 0);
			i = &req->ifcall;
			o = &req->ofcall;
			switch(req->ifcall.type){
			case Twalk:
				rc = twalk(ch, req->fid, req->newfid, i->nwname, i->wname, o->wqid);
				if(rc >= 0)
					o->nwqid = rc;
				break;
			case Topen:
				rc = chanopen(ch, i->mode);
				break;
			case Tcreate:
				rc = chancreat(ch, i->name, i->perm, i->mode);
				if(rc >= 0){
					o->qid = ch->loc->FLoc9.Qid9;
					req->fid->qid = o->qid;
				}
				break;
			case Tread:
				rc = o->count = chanread(ch, o->data, i->count, i->offset);
				break;
			case Twrite:
				rc = o->count = chanwrite(ch, i->data, i->count, i->offset);
				break;
			case Tremove:
				rc = chanremove(ch);
				req->fid->aux = ch = nil;
				break;
			case Tstat:
				rc = chanstat(ch, &req->d);
				break;
			case Twstat:
				rc = chanwstat(ch, &req->d);
				break;
			default:
				werrstr(Einval);
				rc = -1;
			}
			if(rc < 0)
				responderror(req);
			else
				respond(req, nil);
			qlock(&chanqu);
		}
		if(ch != nil){
			ch->wflags &= ~CHWBUSY;
			if((ch->wflags & CHWCLUNK) != 0)
				chanclunk(ch);
		}
	}
}

QLock chanqu;
Chan readych;
Rendez chanre;

void
workerinit(void)
{
	int i;
	
	readych.qnext = readych.qprev = &readych;
	chanre.l = &chanqu;
	for(i = 0; i < NWORKERS; i++)
		threadcreate(workerproc, nil, mainstacksize);
}
