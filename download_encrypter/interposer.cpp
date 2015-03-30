#include "encrypt.h"
#include "boost/thread/shared_mutex.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <unordered_map>
#include <sys/types.h>

#define log(...) printf( __VA_ARGS__ );
//#define log(...)    

extern "C"
{

typedef struct interpose_s {
    void *new_func;
    void *orig_func;
} interpose_t;

int close$NOCANCEL$UNIX2003(int);
int my_open(const char *, int, mode_t);
int my_close(int);
int my_close_nocancel(int);
ssize_t my_read(int fd, void *buf, size_t count);
ssize_t my_write(int fd, const void *buf, size_t count);
int my_dup(int oldfd);
int my_dup2(int oldfd, int newfd);
int my_link(const char *oldpath, const char *newpath);
//int my_rename(const char *oldpath, const char *newpath);
int my_fsync( int fd );

int my_stat(const char *path, struct stat *buf);
int my_fstat(int fd, struct stat *buf);
int my_lstat(const char *path, struct stat *buf);
off_t my_lseek(int fd, off_t offset, int whence);
ssize_t my_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t my_pwrite(int fd, const void *buf, size_t count, off_t offset);




static const interpose_t interposers[] \
    __attribute__ (( used, section("__DATA, __interpose"))) = {
        { (void *)my_open,  (void *)open  }
        , { (void *)my_close, (void *)close }
        , { (void *)my_read, (void *)read }
        , { (void *)my_write, (void *)write }
        , { (void *)my_dup, (void*)dup }
        , { (void *)my_dup2, (void*)dup2 }
        , { (void *)my_link, (void *)link }
        //, { (void *)my_renameat2, (void *)renameat2 }
        //, { (void *)my_rename, (void *)rename }
        //, { (void *)my_fsync, (void *)fsync }
        , { (void *)my_lseek, (void *)lseek }
        , { (void *)my_pread, (void *)pread }
        , { (void *)my_pwrite, (void *)pwrite }
        , { (void *)my_close_nocancel, (void *)close$NOCANCEL$UNIX2003 }
        //{ (void *)my_dup3, (void*)dup3 }  
    };
}

class BufferWriter : public WriteableStream
{
public:
    BufferWriter()
        : m_start( 0 )
        , m_end( 0 )
        , m_lastCount( 0 )
    {
    }

    void SetBuffer( void* buf, size_t count )
    {
        m_start = reinterpret_cast< unsigned char* >( buf );
        m_end = m_start + count;
    }

    virtual void Write( BData data )
    {
        WriteInternal( data, false );
    }

    virtual void EndWrite()
    {
    }

    size_t GetMinReadCount() const
    {
        return 16L;
    }

    size_t Getm_LastCount() const 
    {
        return m_lastCount;
    }

    bool AnyBuffered() const
    {
        return m_spilledOutData.size() > 0;
    }

    size_t FlushPreExisting()
    {
        if ( !m_spilledOutData.size() )
            return 0;

        WriteInternal( BData( &m_spilledOutData[ 0 ], m_spilledOutData.size() ), true );
        m_spilledOutData.erase( m_spilledOutData.begin(), m_spilledOutData.begin() + m_lastCount );
        return m_lastCount;
    }

private:
    void WriteInternal( BData data, bool bFlushing )
    {
        m_lastCount = data.second;
        
        if ( !data.first || !data.second )
            return;

        if ( m_end > m_start ) 
        {
            size_t m = std::min( (size_t) (m_end - m_start), data.second );
            memcpy( m_start, data.first, m );
            m_start += m;
            data.first += m;
            data.second -= m;
            m_lastCount -= data.second;
        }
        
        if ( data.second && !bFlushing )
        {
            size_t li = m_spilledOutData.size();
            m_spilledOutData.resize( m_spilledOutData.size() + data.second );
            memcpy( &m_spilledOutData[ li ] , data.first, data.second );
        }
    }

    std::vector< unsigned char > m_spilledOutData;
    unsigned char* m_start;
    unsigned char* m_end;
    size_t m_lastCount;
};

class FileSpecificState
{
public:
    FileSpecificState( int fd, bool forWriting )
        : m_fdsink( forWriting ? fd : 0 )
        , m_mutex( new boost::shared_mutex() )
        , m_bytecount( 0 )
        , m_refCount( 1 )
    {}
    
    void SetupForTransformation()
    {
        m_tran = CreateCryptingWriter( "password", 
            m_fdsink.GetFd() != 0, 
            m_fdsink.GetFd() != 0 ? static_cast<WriteableStream*>(&m_fdsink) : static_cast<WriteableStream*>( &m_bsink ) );
    }

    FdWriter m_fdsink;
    BufferWriter m_bsink;
    std::shared_ptr< WriteableStream > m_tran;
    std::shared_ptr< boost::shared_mutex > m_mutex;
    size_t m_bytecount;
    size_t m_refCount;
    std::string m_path;

    bool NotGoodForReading() const
    {
        return m_fdsink.GetFd() != 0;
    }
};

class FileIOHandler
{
public:
    static FileIOHandler& The()
    {
        FileIOHandler* p = s_instance.load( std::memory_order_acq_rel );
        if ( p )
            return *p;
        
        std::unique_ptr< FileIOHandler > f( new FileIOHandler() );
        if ( s_instance.compare_exchange_strong( p, f.get(), std::memory_order_acq_rel, std::memory_order_acq_rel ) )
        {
            s_unique = std::move( f );
            return *s_unique;
        }

        return *s_instance.load( std::memory_order_acq_rel );
    }

    void Add( int fd, int flags, const char* path )
    {
        if ( (flags & O_RDWR) == O_RDWR )
        {
            log( "%d Fd opened for read write - not handling ", fd );
            return;
        }

        boost::upgrade_lock<boost::shared_mutex> lock( m_mutex );
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        auto iter = m_fdMap.find( fd );
        if ( iter != m_fdMap.end() )
        {
            ++iter->second.m_refCount;
            log("%d FD is being added again\n", fd );
            return;
        }

        auto inserted = m_fdMap.insert( std::make_pair( fd, FileSpecificState( fd, (flags & O_WRONLY) == O_WRONLY ) ) );
        inserted.first->second.SetupForTransformation();
        inserted.first->second.m_path = path;
    }

    ssize_t Read( int fd, void* bufInput, size_t count )
    {
        FileSpecificState* pFss = GetFSS( fd );
        
        if ( !pFss || pFss->NotGoodForReading() )
            return read( fd, bufInput, count );

        log ("reading from FD %d, bufsize = %zu \n", fd, count );
        FileSpecificState& fss = *pFss;
        boost::upgrade_lock<boost::shared_mutex> lock( * (fss.m_mutex) );
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        size_t minCount = fss.m_bsink.GetMinReadCount();
        size_t initialCount = count;
        unsigned char buf[32*1024];
        ssize_t n = 0;
        fss.m_bsink.SetBuffer( bufInput, count );
        
        for ( count -= fss.m_bsink.FlushPreExisting();
                count && ( n = read( fd, buf, std::min( std::max( count, minCount ), sizeof(buf) ) ) ) > 0; 
                count -= fss.m_bsink.Getm_LastCount() )
        {
            fss.m_tran->Write( std::make_pair( buf, (size_t)n ) );
        }

        if ( n < 0 )
        {
            log( "%s %s %ld\n", __FUNCTION__ , " returing error - ", n );
            return n;
        }

        if ( n == 0 )
        {
            fss.m_tran->EndWrite();
            return fss.m_bsink.AnyBuffered() ? initialCount - count : n;
        }

        return initialCount - count;
    }

    ssize_t Write( int fd, const void* buf, size_t count )
    {
        FileSpecificState* pFss = GetFSS( fd );

        if ( !pFss )
            return write( fd, buf, count );

        boost::upgrade_lock<boost::shared_mutex> lock( *(pFss->m_mutex) );
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        pFss->m_tran->Write( BData( (unsigned char*) buf, count ) );
        return count;
    }

    bool Remove( int fd )
    {
        boost::upgrade_lock<boost::shared_mutex> lock( m_mutex );
        boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

        auto iter = m_fdMap.find( fd );
        if ( iter == m_fdMap.end() )
        {
            return false;
        }

        if ( iter->second.NotGoodForReading() )
        {
            iter->second.m_tran->EndWrite();
        }

        m_fdMap.erase( iter );

        return true;
    }

    bool IsFDMonitored( int fd )
    {
        return GetFSS( fd ) != 0;
    }

private:
    static std::atomic<FileIOHandler*> s_instance;
    static std::unique_ptr<FileIOHandler> s_unique;
    std::unordered_map< int, FileSpecificState > m_fdMap;
    boost::shared_mutex m_mutex;

    FileSpecificState* GetFSS( int fd )
    {
        boost::shared_lock<boost::shared_mutex> lock( m_mutex );

        auto iter = m_fdMap.find( fd );
        if ( iter != m_fdMap.end() )
        {
            return &iter->second;
        }

        return 0;
    }

    FileIOHandler()
    {}
};

std::atomic<FileIOHandler*> FileIOHandler::s_instance;
std::unique_ptr<FileIOHandler> FileIOHandler::s_unique;

extern "C"
{

int my_open(const char *path, int flags, mode_t mode)
{    
    int ret = open(path, flags, mode);
    if ( !path || !strstr( path, "Downloads" ) || ret == -1 )
        return ret;
    
    const char lookingFor[] = "putty-0.63.tar.gz";
    const char* p = strstr( path, lookingFor );
    if ( p && !p[ sizeof( lookingFor ) - 1 ] )
    {
        log("%s\n", "Got the expected file" );
    }

    log("--> %d = open(%s, %x, %x)\n", ret, path, flags, mode);
    FileIOHandler::The().Add( ret, flags, path );
    return ret;
}

int my_close(int d)
{
    bool wasMonitored = FileIOHandler::The().Remove( d );
    int ret = close(d);
    if ( wasMonitored )
        log("--> %d = close(%d) \n", ret, d );
    return ret;
}

int my_close_nocancel( int d )
{
    bool wasMonitored = FileIOHandler::The().Remove( d );
    int ret = close$NOCANCEL$UNIX2003(d);
    if ( wasMonitored )
        log("--> %d = close(%d) \n", ret, d );
    return ret;
}

ssize_t my_read(int fd, void *buf, size_t count)
{
    return FileIOHandler::The().Read( fd, buf, count );
}

ssize_t my_write(int fd, const void *buf, size_t count)
{
    return FileIOHandler::The().Write( fd, buf, count );
}

int my_dup(int oldfd)
{
    int r = dup( oldfd );
    return r;
}

int my_dup2(int oldfd, int newfd)
{
    int r = dup2( oldfd, newfd );
    if ( FileIOHandler::The().IsFDMonitored( oldfd ) || FileIOHandler::The().IsFDMonitored( newfd ) )
        log("%d = dup2( %d, %d )\n", r, oldfd, newfd );
    return r;   
}
int my_link(const char *oldpath, const char *newpath)
{
    int r = link( oldpath, newpath );
    log( "%d = link( %s, %s )", r, oldpath, newpath );
    return r;
}

int my_fsync( int fd )
{
    bool wasMonitored = FileIOHandler::The().Remove( fd );
    int r = fsync( fd );
    if ( wasMonitored )
        log( "%d = fsync( %d )\n", r, fd );
    return r;
}

int my_stat(const char *path, struct stat *buf)
{
    int r = stat( path, buf );
    log("%d = %s( %s )", r, "stat", path );
    return r;
}

int my_fstat(int fd, struct stat *buf)
{
    int r = fstat( fd, buf );
    log("%d = %s( %d )", r, "fstat", fd );
    return r;
}

int my_lstat(const char *path, struct stat *buf)
{
    int r = lstat( path, buf );
    log("%d = %s( %s )", r, "lstat", path );
    return r;
}

off_t my_lseek(int fd, off_t offset, int whence)
{
    off_t r = lseek( fd, offset, whence );
    if ( FileIOHandler::The().IsFDMonitored( fd ) )
        log("%lld = %s( %d, %lld, %d ) \n", r, "lseek", fd, offset, whence );
    return r;
}

ssize_t my_pread(int fd, void *buf, size_t count, off_t offset)
{
    auto r = pread( fd, buf, count, offset );
    if ( FileIOHandler::The().IsFDMonitored( fd ) )
        log( "%ld = %s( %d, %zu, %lld )\n", r, "pread", fd, count, offset );
    return r;
}

ssize_t my_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    auto r = pwrite( fd, buf, count, offset );
    if ( FileIOHandler::The().IsFDMonitored( fd ) )
        log( "%ld = %s( %d, %zu, %lld )\n", r, "pwrite", fd, count, offset );
    return r;
}

}

// int my_dup3(int oldfd, int newfd, int flags)
// {
//     int r = dup3( oldfd, newfd, flags );
//     log("%d = dup3( %d, %d, %d )", r, oldfd, newfd, flags );
//     return r;
// }

// int my_renameat2(int olddirfd, const char *oldpath,
//                      int newdirfd, const char *newpath, unsigned int flags)
// {
//     int r = renameat2( olddirfd, oldpath, newdirfd, newpath, flags );
//     log("%d = %s( %d, %s, %d, %s, %u )\n", r, "renameat2", olddirfd, oldpath, newdirfd, newpath, flags );
//     return r;
// }

/*
record the file descriptors opened for files under Downloads directory
and associate that with a CryptingWriter. We need to be able to handle read/write 
but for now we will just write a log on the console.

A Write to the descrption is piped as below,

write buffer to CW --> FD ( the file descriptor writer )

A read from the descriptor would be piped as below

Read to internal buffer from FD then CW --> read buffer

*/