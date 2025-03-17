#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <dlfcn.h>
#include <cxxabi.h>
#include <execinfo.h>
#include <regex.h>
#include <sstream>
#include <stdlib.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>

#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>

#include "HeapAlloc.h"

using namespace std;

// arrange for init to be called on dlopen ...
static void init(void) __attribute__ ((constructor));

// ----------------------------------------------------------------

namespace
{
    enum { BEFORE, DURING, AFTER };

    static int g_initstate = BEFORE;
    static void *(*g_real_malloc) (size_t) = 0;
    static void *(*g_real_calloc) (size_t, size_t) = 0;
    static void (*g_real_free) (void *) = 0;
    static void *(*g_real_realloc) (void *, size_t) = 0;

    char g_bootstrap[1024*1024];

    pthread_t g_threadid;


// ----------------------------------------------------------------

class MyHeap
{
public:
    static void * alloc(size_t sz)
    {
        return (*g_real_malloc)(sz);
    }

    static void dealloc(void * ptr)
    {
        (*g_real_free)(ptr);
    }
};

class Node
{
public:
    static const int NFRAMES = 32;
    static const int HEADSZ = 64;

    Node(void * ptr, size_t sz)
        : m_marked(false)
        , m_ptr(ptr)
        , m_size(sz)
    {
        memset(m_head, 0, HEADSZ);
        memset(m_btdata, 0, sizeof(m_btdata));

        // insert a backtrace in this node
        m_btsize = backtrace(m_btdata, NFRAMES);
    }

    Node(const Node & i)
        : m_marked(i.m_marked)
        , m_ptr(i.m_ptr)
        , m_size(i.m_size)
        , m_btsize(i.m_btsize)
    {
        memcpy(m_head, i.m_head, sizeof(m_head));
        memcpy(m_btdata, i.m_btdata, sizeof(m_btdata));
    }

    Node & operator=(const Node & rhs)
    {
        m_marked = rhs.m_marked;
        m_ptr = rhs.m_ptr;
        m_size = rhs.m_size;
        m_btsize = rhs.m_btsize;
        memcpy(m_head, rhs.m_head, sizeof(m_head));
        memcpy(m_btdata, rhs.m_btdata, sizeof(m_btdata));
        return *this;
    }

    ~Node() {}

    size_t const & size() { return m_size; }

    bool isMarked() const { return m_marked; }

    void setMarked(bool marked) { m_marked = marked; }

    bool operator<(const Node & b) const
    {
        // Sort Nodes by their backtraces.
        return memcmp(m_btdata, b.m_btdata, sizeof(m_btdata));
    }

    void snapshotHead()
    {
        memcpy(m_head, m_ptr, std::min(m_size, (size_t) HEADSZ));
    }

private:
    friend ostream & operator<<(ostream & ostrm, const Node & node)
    {
        ostrm << "-----------------------------------------------------";
        ostrm << endl;
        ostrm << "SIZE=" << std::dec << node.m_size
              << " ADDRESS=" << node.m_ptr;
        ostrm << endl;

        ostrm << "STR[0:" << HEADSZ << "]=\"";
        size_t len = std::min(node.m_size, (size_t) HEADSZ);
        for (unsigned odi = 0; odi < len; ++odi)
        {
            int c = node.m_head[odi];
            if (isprint(c))
                ostrm << static_cast<char>(c);
            else
                ostrm << ' ';
        }
        ostrm << "\"" << endl;

        char ** funcname = backtrace_symbols(node.m_btdata, node.m_btsize);
        ostrm << "BEGIN BACKTRACE" << endl;

        for (unsigned i = 0; i < node.m_btsize; ++i)
        {
            bool unmapped = true;

            Dl_info dlinfo;
            if (dladdr(node.m_btdata[i], & dlinfo) != 0)
            {
                int status;
                char * demangled = abi::__cxa_demangle(dlinfo.dli_sname,
                                                       NULL,
                                                       NULL,
                                                       &status);
                if (demangled)
                {
                    // Print the demangled name.
                    unmapped = false;
                    ostrm << "  " << demangled << endl;
                    free(demangled);
                }
            }

            if (unmapped)
            {
                // Couldn't find symbol, print the file and address.
                ostrm << "  " << funcname[i] << endl;
            }
        }

        ostrm << "END BACKTRACE" << endl;
        free(funcname);

        return ostrm;
    }

    bool				m_marked;
    void *				m_ptr;
    size_t				m_size;
    char				m_head[HEADSZ];

    void *				m_btdata[NFRAMES];
    size_t				m_btsize;
};

namespace
{
    typedef HeapAlloc<Node, MyHeap> NodeAlloc;

    typedef std::map<void *, Node, std::less<void *>, NodeAlloc> NodeTable;

    typedef std::vector<Node, NodeAlloc> NodeSeq;

    typedef std::map<Node, std::pair<size_t, size_t>, std::less<Node>, NodeAlloc> NodeMap;

    typedef std::pair<Node, std::pair<size_t, size_t> > NodeSize;

    typedef HeapAlloc<NodeSize, MyHeap> NodeSizeAlloc;

    typedef std::vector<NodeSize, NodeSizeAlloc> NodeSizeSeq;

    pthread_mutex_t		g_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_key_t 		g_inrptkey;
    pthread_key_t 		g_reentkey;

    NodeTable *			g_nodes;
}

// ----------------------------------------------------------------

bool by_totalmem_desc(NodeSize const & ns1, NodeSize const & ns2)
{
    return ns1.second.second >= ns2.second.second;
}

void report(bool flag)
{
    char const * heapmondir = getenv("HEAPMONDIR");
    if (!heapmondir)
        heapmondir = ".";

#if 0
    NodeSeq found;

    pthread_setspecific(g_inrptkey, (void *) 1);

    pthread_mutex_lock(&g_mutex);

    NodeTable::iterator pos;
    for (pos = g_nodes->begin(); pos != g_nodes->end(); ++pos)
    {
        if (!pos->second.isMarked())
        {
            pos->second.snapshotHead();
            pos->second.setMarked(true);
            found.push_back(pos->second);
        }
    }

    pthread_mutex_unlock(&g_mutex);

    ostringstream path;
    path << heapmondir << "/heapmon-" << getpid() << ".log";
    ofstream logout(path.str().c_str(), ios::app);

    if (flag)
    {
        // print them outside the lock (print routines use heap ...)
        for (NodeSeq::iterator pos = found.begin(); pos != found.end(); ++pos)
            logout << *pos << endl;
    }

    pthread_setspecific(g_inrptkey, (void *) 0);
#else
    NodeMap found;

    pthread_setspecific(g_inrptkey, (void *) 1);

    pthread_mutex_lock(&g_mutex);

    NodeTable::iterator pos;
    for (pos = g_nodes->begin(); pos != g_nodes->end(); ++pos)
    {
        if (!pos->second.isMarked())
        {
            pos->second.snapshotHead();
            pos->second.setMarked(true);

            NodeMap::iterator pos2 = found.find(pos->second);
            if (pos2 == found.end())
            {
                // This is a new entry
                found.insert(make_pair(pos->second,
                                       make_pair(1, pos->second.size())));
            }
            else
            {
                // This is an existing entry, add count and size.
                pos2->second.first += 1;
                pos2->second.second += pos->second.size();
            }
        }
    }

    pthread_mutex_unlock(&g_mutex);

    ostringstream path;
    path << heapmondir << "/heapmon-" << getpid() << ".log";
    ofstream logout(path.str().c_str(), ios::app);

    // Sort the node/size pairs by total memory, descending.
    NodeSizeSeq nss;
    nss.reserve(found.size());
    for (NodeMap::iterator pos = found.begin(); pos != found.end(); ++pos)
    {
        nss.push_back(make_pair(pos->first,
                                make_pair(pos->second.first,
                                          pos->second.second)));
    }
    sort(nss.begin(), nss.end(), by_totalmem_desc);

    if (flag)
    {
        // print them outside the lock (print routines use heap ...)
        for (NodeSizeSeq::iterator it = nss.begin(); it != nss.end(); ++it)
        {
            logout << it->first
                   << "NUMBLOCKS=" << it->second.first << endl
                   << "TOTALMEM=" << it->second.second << endl
                   << endl;
        }
    }

    pthread_setspecific(g_inrptkey, (void *) 0);
#endif
}

// ----------------------------------------------------------------

void *
thread_run(void * arg)
{
    struct stat statbuf;
    time_t lastmtime;
    bool flag = false;

    char const * heapmondir = getenv("HEAPMONDIR");
    if (!heapmondir)
        heapmondir = ".";

    ostringstream path;
    path << heapmondir << "/heapmon-" << getpid() << ".ctl";

    // create the control file (only interested in it's mtime ...)
    {
        ofstream ctl(path.str().c_str());
    }
    stat(path.str().c_str(), &statbuf);
    lastmtime = statbuf.st_mtime;

    // run a report every time the control file is touched ...
    while (true)
    {
        sleep(1);
        stat(path.str().c_str(), &statbuf);
        if (lastmtime != statbuf.st_mtime)
        {
            lastmtime = statbuf.st_mtime;
            report(flag);
            flag = true;
        }
    }

    return NULL;
}

// ----------------------------------------------------------------


void *
init_malloc(size_t sz)
{
    static char * ptr = g_bootstrap;

    // cerr << "init_malloc " << sz << " bytes" << endl;

    char * chunk = ptr;
    ptr += sz;

    assert(ptr < g_bootstrap + sizeof(g_bootstrap));

    return reinterpret_cast<void *>(chunk);
}

void *
init_calloc(size_t nmemb, size_t sz)
{
    size_t totsz = nmemb * sz;

    void * optr = init_malloc(totsz);
    memset(optr, 0, totsz);

    return optr;
}

void *
init_realloc(void * iptr, size_t sz)
{
    void * optr = init_malloc(sz);

    // This appears unsafe, but since the input ptr is only
    // NULL or allocated on our bootstrap array it is OK.
    //
    if (iptr)
        memcpy(optr, iptr, sz);

    return optr;
}

[[maybe_unused]] void
init_free(void * ptr)
{
    // do nothing ...
}

} // end namespace

void
init()
{
    if (g_initstate != BEFORE)
        return;

    g_initstate = DURING;

    assert(pthread_key_create(&g_inrptkey, NULL) == 0);
    assert(pthread_key_create(&g_reentkey, NULL) == 0);

    // Static initializer used instead ...
    // assert(pthread_mutex_init(&g_mutex, NULL) == 0);

    assert(pthread_create(&g_threadid, NULL, thread_run, NULL) == 0);

    g_real_malloc = (void *(*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    assert(g_real_malloc);

    g_real_calloc = (void *(*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
    assert(g_real_calloc);

    g_real_free = (void (*)(void *)) dlsym(RTLD_NEXT, "free");
    assert(g_real_free);

    g_real_realloc = (void *(*)(void *, size_t)) dlsym(RTLD_NEXT, "realloc");
    assert(g_real_realloc);

    g_nodes = new NodeTable;

    g_initstate = AFTER;
}


extern "C"
{
    void *
    malloc(size_t sz)
    {
        if (g_initstate == BEFORE)
            init();

        if (g_initstate == DURING)
            return init_malloc(sz);

        // if we get here g_initstate == AFTER

        void * ptr = (*g_real_malloc)(sz);

        long inreport = (long) pthread_getspecific(g_inrptkey);
        long entered = (long) pthread_getspecific(g_reentkey);

        if (!inreport && !entered)
        {
            // cerr << "malloc(" << sz << ") = " << ptr << endl;

            pthread_setspecific(g_reentkey, (void *) 1);
            pthread_mutex_lock(&g_mutex);
            g_nodes->insert(make_pair(ptr, Node(ptr, sz)));
            pthread_mutex_unlock(&g_mutex);
            pthread_setspecific(g_reentkey, (void *) 0);
        }

        return ptr;
    }

    void *
    calloc(size_t nmemb, size_t sz)
    {
        if (g_initstate == BEFORE)
            init();

        if (g_initstate == DURING)
            return init_calloc(nmemb, sz);

        // if we get here g_initstate == AFTER

        void * ptr = (*g_real_calloc)(nmemb, sz);

        size_t totsz = nmemb * sz;

        long inreport = (long) pthread_getspecific(g_inrptkey);
        long entered = (long) pthread_getspecific(g_reentkey);

        if (!inreport && !entered)
        {
            // cerr << "malloc(" << sz << ") = " << ptr << endl;

            pthread_setspecific(g_reentkey, (void *) 1);
            pthread_mutex_lock(&g_mutex);
            g_nodes->insert(make_pair(ptr, Node(ptr, totsz)));
            pthread_mutex_unlock(&g_mutex);
            pthread_setspecific(g_reentkey, (void *) 0);
        }

        return ptr;
    }

    void
    free(void * ptr)
    {
        if (!ptr)
            return;

        if (ptr >= g_bootstrap && ptr < g_bootstrap + sizeof(g_bootstrap))
            return;

        long inreport = (long) pthread_getspecific(g_inrptkey);
        long entered = (long) pthread_getspecific(g_reentkey);

        if (!inreport && !entered)
        {
            // cerr << "free(" << ptr << ")" << " starting" << endl;

            pthread_mutex_lock(&g_mutex);
            g_nodes->erase(ptr);
            pthread_mutex_unlock(&g_mutex);
        }

        (*g_real_free)(ptr);
    }

    void *
    realloc(void * ptr, size_t sz)
    {
        if (g_initstate == BEFORE)
            init();

        if (g_initstate == DURING)
            return init_realloc(ptr, sz);

        // if we get here g_initstate == AFTER

        void * retptr = (*g_real_realloc)(ptr, sz);

        if (sz == 0)
        {
            // this is the same as a free
            pthread_mutex_lock(&g_mutex);
            g_nodes->erase(ptr);
            pthread_mutex_unlock(&g_mutex);

            return retptr;
        }

        if (retptr == NULL)
        {
            // nothing happened, just return
            return retptr;
        }

        // remove the old entry and insert the new
        pthread_mutex_lock(&g_mutex);
        g_nodes->erase(ptr);
        g_nodes->insert(make_pair(retptr, Node(retptr, sz)));
        pthread_mutex_unlock(&g_mutex);

        return retptr;
    }

} // end extern "C"

// ----------------------------------------------------------------
// Local Variables:
// mode: C++
// tab-width: 4
// c-basic-offset: 4
// End:
