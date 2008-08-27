// Copyright (C) 2006-2007 David Sugar, Tycho Softworks.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

#include <config.h>
#include <ucommon/memory.h>
#include <ucommon/thread.h>
#include <ucommon/string.h>
#ifdef	HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <limits.h>
#include <string.h>
#include <stdio.h>

using namespace UCOMMON_NAMESPACE;

static size_t cpr_pagesize(void)
{
#ifdef  HAVE_SYSCONF
    return sysconf(_SC_PAGESIZE);
#elif defined(PAGESIZE)
    return PAGESIZE;
#elif defined(PAGE_SIZE)
    return PAGE_SIZE;
#else
    return 1024;
#endif
}

mempager::mempager(size_t ps)
{
#ifdef	HAVE_SYSCONF
	size_t paging = sysconf(_SC_PAGESIZE);
#elif defined(PAGESIZE)
	size_t paging = PAGESIZE;
#elif defined(PAGE_SIZE)
	size_t paging = PAGE_SIZE;
#else
	size_t paging = 1024;
#endif

	pthread_mutex_init(&mutex, NULL);

	if(!ps)
		ps = paging;
	else if(ps > paging)
		ps = (((ps + paging - 1) / paging)) * paging;

#ifdef	HAVE_POSIX_MEMALIGN
	if(ps >= paging)
		align = sizeof(void *);
	else
		align = 0;

	switch(align)
	{
	case 2:
	case 4:
	case 8:
	case 16:
		break;
	default:
		align = 0;
	}
#endif
	pagesize = ps;
	count = 0;
	limit = 0;
	page = NULL;
}

mempager::~mempager()
{
	mempager::purge_locked();
	pthread_mutex_destroy(&mutex);
}

unsigned mempager::utilization(void)
{
	unsigned long used = 0, alloc = 0;
	page_t *mp = page;

	while(mp) {
		alloc += pagesize;
		used += mp->used;
		mp = mp->next;
	}

	if(!used)
		return 0;

	alloc /= 100;
	used /= alloc;
	return used;
}

void mempager::purge_locked(void)
{
	page_t *next;
	while(page) {
		next = page->next;
		free(page);
		page = next;
	}
	count = 0;
}


void mempager::purge(void)
{
	pthread_mutex_lock(&mutex);	
	purge_locked();
	pthread_mutex_unlock(&mutex);
}

mempager::page_t *mempager::pager(void)
{
	page_t *npage = NULL;
#ifdef	HAVE_POSIX_MEMALIGN
	void *addr;
#endif

	crit(!limit || count < limit, "mempager limit reached");

#ifdef	HAVE_POSIX_MEMALIGN
	if(align && !posix_memalign(&addr, align, pagesize)) {
		npage = (page_t *)addr;
		goto use;
	}
#endif
	npage = (page_t *)malloc(pagesize);

#ifdef	HAVE_POSIX_MEMALIGN
use:
#endif
	crit(npage != NULL, "mempager alloc failed");

	++count;
	npage->used = sizeof(page_t);
	npage->next = page;
	page = npage;
	if((size_t)(npage) % sizeof(void *))
		npage->used += sizeof(void *) - ((size_t)(npage) % sizeof(void 
*));
	return npage;
}

void mempager::dealloc(void *mem)
{
}

void *mempager::alloc_locked(size_t size)
{
	assert(size > 0);

	caddr_t mem;
	page_t *p = page;

	crit(size <= (pagesize - sizeof(page_t)), "mempager alloc failed");

	while(size % sizeof(void *))
		++size;

	while(p) {
		if(size <= pagesize - p->used)	
			break;
		p = p->next;
	}
	if(!p)
		p = pager();

	mem = ((caddr_t)(p)) + p->used;	
	p->used += size;
	return mem;
}

void *mempager::alloc(size_t size)
{
	assert(size > 0);

	void *mem;

	crit(size <= (pagesize - sizeof(page_t)), "mempager page size exceeded");

	while(size % sizeof(void *))
		++size;

	pthread_mutex_lock(&mutex);
	mem = alloc_locked(size);
	pthread_mutex_unlock(&mutex);
	return mem;
}

char *mempager::dup(const char *str)
{
	if(!str)
		return NULL;
	size_t len = strlen(str) + 1;
	char *mem = static_cast<char *>(alloc(len));
	String::set(mem, len, str);
	return mem;
}

char *mempager::dup_locked(const char *str)
{
    if(!str)
        return NULL;
	size_t len = strlen(str) + 1;
    char *mem = static_cast<char *>(alloc_locked(len));
    String::set(mem, len, str);
    return mem;
}

char *mempager::dup_locked(void *obj, size_t size)
{
	assert(obj != NULL);
	assert(size > 0);

    char *mem = static_cast<char *>(alloc_locked(size));
	memcpy(mem, obj, size);
    return mem;
}

void *mempager::dup(void *obj, size_t size)
{
	assert(obj != NULL);
	assert(size > 0);

	void *mem = alloc(size);
	memcpy(mem, obj, size);
	return mem;
}

autorelease::autorelease()
{
	pool = NULL;
}

autorelease::~autorelease()
{
	release();
}

void autorelease::release()
{
	LinkedObject *obj;

	while(pool) {
		obj = pool;
		pool = obj->getNext();
		obj->release();
	}
}

void autorelease::operator+=(LinkedObject *obj)
{
	assert(obj != NULL);

	obj->enlist(&pool);
}

PagerObject::PagerObject() :
LinkedObject(NULL), CountedObject()
{
}

void PagerObject::dealloc(void)
{
	pager->put(this);
}

void PagerObject::release(void)
{
	CountedObject::release();
}

PagerPool::PagerPool(mempager *p) 
{
	pager = p;
	freelist = NULL;
	pthread_mutex_init(&mutex, NULL);
}

PagerPool::~PagerPool()
{
	pthread_mutex_destroy(&mutex);
}

void PagerPool::put(PagerObject *ptr)
{
	assert(ptr != NULL);

    pthread_mutex_lock(&mutex);
	ptr->enlist(&freelist);
    pthread_mutex_unlock(&mutex);
}

PagerObject *PagerPool::get(size_t size)
{
	assert(size > 0);

	PagerObject *ptr;
	pthread_mutex_lock(&mutex);
	ptr = static_cast<PagerObject *>(freelist);
	if(ptr) 
		freelist = ptr->next;

    pthread_mutex_unlock(&mutex);

	if(!ptr && pager)
		ptr = new((caddr_t)(pager->alloc(size))) PagerObject;
	else if(!ptr && !pager)
		ptr = new(size - sizeof(PagerObject)) PagerObject;
	memset(ptr, 0, size);
	ptr->pager = this;
	return ptr;
}

keyassoc::keydata::keydata(keyassoc *assoc, char *kid, unsigned max, unsigned bufsize) :
NamedObject(assoc->root, kid, max)
{
	assert(assoc != NULL);
	assert(kid != NULL && *kid != 0);
	assert(max > 1);

	String::set(text, bufsize, kid);
	data = NULL;
	id = text;
}

keyassoc::keyassoc(unsigned pathmax, size_t strmax, size_t ps) :
mempager(ps)
{
	assert(pathmax > 1);
	assert(strmax > 1);
	assert(ps > 1);

	paths = pathmax;
	keysize = strmax;
	count = 0;

	root = (NamedObject **)alloc(sizeof(NamedObject *) * pathmax);
	memset(root, 0, sizeof(NamedObject *) * pathmax);
	if(keysize) {
		list = (LinkedObject **)alloc(sizeof(LinkedObject *) * (keysize / 8));
		memset(list, 0, sizeof(LinkedObject *) * (keysize / 8));
	}
	else
		list = NULL;
};

keyassoc::~keyassoc()
{
	purge();
}

void keyassoc::purge(void)
{
	mempager::purge();
	list = NULL;
	root = NULL;
}

void *keyassoc::locate(const char *id)
{
	assert(id != NULL && *id != 0);

	keydata *kd;

	lock();
	kd = static_cast<keydata *>(NamedObject::map(root, id, paths));
	unlock();
	if(!kd)
		return NULL;

	return kd->data;
}

void *keyassoc::remove(const char *id)
{
	assert(id != NULL && *id != 0);

	keydata *kd;
	LinkedObject *obj;
	void *data;
	unsigned path = NamedObject::keyindex(id, paths);
	unsigned size = strlen(id);

	if(!keysize || size >= keysize || !list)
		return NULL;

	lock();
	kd = static_cast<keydata *>(NamedObject::map(root, id, paths));
	if(!kd) {
		unlock();
		return NULL;
	}
	data = kd->data;
	obj = static_cast<LinkedObject*>(kd);
	obj->delist((LinkedObject**)(&root[path]));
	obj->enlist(&list[size / 8]);
	--count;
	unlock();
	return data;
}

bool keyassoc::create(char *id, void *data)
{
	assert(id != NULL && *id != 0);
	assert(data != NULL);

    keydata *kd;
	LinkedObject *obj;
	unsigned size = strlen(id);

	if(keysize && size >= keysize)
		return false;

	lock();
	kd = static_cast<keydata *>(NamedObject::map(root, id, paths));
	if(kd) {
		unlock();
		return false;
	}
	caddr_t ptr = NULL;
	size /= 8;
	if(list && list[size]) {
		obj = list[size];
		list[size] = obj->getNext();
		ptr = (caddr_t)obj;
	}
	if(ptr == NULL)
		ptr = (caddr_t)alloc_locked(sizeof(keydata) + size * 8);
	kd = new(ptr) keydata(this, id, paths, 8 + size * 8);					
	kd->data = data;
	++count;
	unlock();
	return true;
}

bool keyassoc::assign(char *id, void *data)
{
	assert(id != NULL && *id != 0);
	assert(data != NULL);

    keydata *kd;
	LinkedObject *obj;
	unsigned size = strlen(id);

	if(keysize && size >= keysize)
		return false;

	lock();
	kd = static_cast<keydata *>(NamedObject::map(root, id, paths));
	if(!kd) {
		caddr_t ptr = NULL;
		size /= 8;
		if(list && list[size]) {
			obj = list[size];
			list[size] = obj->getNext();
			ptr = (caddr_t)obj;
		}
		if(ptr == NULL)
			ptr = (caddr_t)alloc_locked(sizeof(keydata) + size * 8);
		kd = new(ptr) keydata(this, id, paths, 8 + size * 8);				
		++count;	
	}
	kd->data = data;
	unlock();
	return true;
}


