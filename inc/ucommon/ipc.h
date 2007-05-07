#ifndef _UCOMMON_IPC_H_
#define	_UCOMMON_IPC_H_

#ifndef	_UCOMMON_CPR_H_
#include <ucommon/cpr.h>
#endif

NAMESPACE_UCOMMON

class __EXPORT MappedMemory
{
private:
	caddr_t map;
	fd_t fd;

protected:
	size_t size, used;

	virtual void fault(void);

public:
	MappedMemory(const char *fname, size_t len = 0);
	virtual ~MappedMemory();

	inline operator bool() const
		{return (size != 0);};

	inline bool operator!() const
		{return (size == 0);};

	void *sbrk(size_t size);
	void *get(size_t offset);

	inline size_t len(void)
		{return size;};
};

class __EXPORT MappedReuse : protected ReusableAllocator, protected MappedMemory
{
private:
	unsigned objsize;

protected:
	MappedReuse(const char *name, size_t osize, unsigned count);

	bool avail(void);
	ReusableObject *request(void);
	ReusableObject *get(void);
	ReusableObject *get(timeout_t timeout);
};

class __EXPORT MessageQueue
{
private:
	struct ipc;

	ipc *mq;

public:
	MessageQueue(const char *name, size_t objsize, unsigned count);
	MessageQueue(const char *name);
	~MessageQueue();

	bool get(void *data, size_t len);
	bool put(void *data, size_t len);
	bool puts(char *data);
	bool gets(char *data);

	void release(void);

	inline operator bool() const
		{return mq != NULL;};
		
	inline bool operator!() const
		{return mq == NULL;};

	inline bool isPending(void) const
		{return getPending() > 0;};

	unsigned getPending(void) const;
};

class __EXPORT env : public mempager
{
protected:
	LinkedObject *root;

public:
	enum
	{
		SPAWN_WAIT = 0,
		SPAWN_NOWAIT,
		SPAWN_DETACH
	};

	typedef named_value<const char *> member;

	env(size_t paging);
	~env();

	void dup(const char *id, const char *val);
	void set(char *id, const char *val);
	const char *get(const char *id);

	inline member *find(const char *id)
		{return static_cast<member*>(NamedObject::find(static_cast<NamedObject*>(root), id));};

	inline member *begin(void)
		{return static_cast<member*>(root);};

#ifdef	_MSWINDOWS_
	char **getEnviron(void);
#endif

	static int spawn(const char *fn, char **args, int mode, pid_t *pid, fd_t *iov = NULL, env *ep = NULL);
	static void setenv(env *ep);
};

template <class T>
class mqueue : private MessageQueue
{
protected:
	unsigned getPending(void)
		{return MessageQueue::getPending();};

public:
	inline mqueue(const char *name) :
		MessageQueue(name) {};
	
	inline mqueue(const char *name, unsigned count) :
		MessageQueue(name, sizeof(T), count) {};

	inline ~mqueue() {release();};

	inline operator bool() const
		{return mq != NULL;};

	inline bool operator!() const
		{return mq == NULL;};

	inline bool get(T *buf)
		{return MessageQueue::get(buf, sizeof(T));};
	
	inline bool put(T *buf)
		{return MessageQueue::put(buf, sizeof(T));};
};

template <class T>
class mapped_array : public MappedMemory
{
public:
	inline mapped_array(const char *fn, unsigned members) : 
		MappedMemory(fn, members * sizeof(T)) {};

	inline void addLock(void)
		{sbrk(sizeof(T));};
	
	inline T *operator()(unsigned idx)
		{return static_cast<T*>(get(idx * sizeof(T)));}

	inline T *operator()(void)
		{return static_cast<T*>(sbrk(sizeof(T)));};
	
	inline T &operator[](unsigned idx)
		{return *(operator()(idx));};

	inline unsigned getSize(void)
		{return (unsigned)(size / sizeof(T));};
};

template <class T>
class mapped_reuse : protected MappedReuse
{
public:
	inline mapped_reuse(const char *fname, unsigned count) :
		MappedReuse(fname, sizeof(T), count) {};

	inline operator bool()
		{return MappedReuse::avail();};

	inline bool operator!()
		{return !MappedReuse::avail();};

	inline operator T*()
		{return mapped_reuse::get();};

	inline T* operator*()
		{return mapped_reuse::get();};

	inline T *get(void)
		{return static_cast<T*>(MappedReuse::get());};

    inline T *get(timeout_t timeout)
        {return static_cast<T*>(MappedReuse::get(timeout));};


	inline T *request(void)
		{return static_cast<T*>(MappedReuse::request());};

	inline void release(T *o)
		{MappedReuse::release(o);};
};
	
template <class T>
class mapped_view : protected MappedMemory
{
public:
	inline mapped_view(const char *fn) : 
		MappedMemory(fn) {};
	
	inline const char *id(unsigned idx)
		{return static_cast<const char *>(get(idx * sizeof(T)));};

	inline T *operator()(unsigned idx)
		{return static_cast<const T*>(get(idx * sizeof(T)));}
	
	inline T &operator[](unsigned idx)
		{return *(operator()(idx));};

	inline unsigned getSize(void)
		{return (unsigned)(size / sizeof(T));};
};

END_NAMESPACE

#endif

