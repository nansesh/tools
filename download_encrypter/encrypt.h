#include <utility>
#include <functional>
#include <algorithm>
#include <vector>
#include <string>
#include <cassert>
#include <unistd.h>
#include <errno.h>

typedef std::pair< const unsigned char*, size_t > BData;
#define NULL_BDATA	( std::make_pair( (unsigned char*)0, (size_t) 0 ) )
inline BData Head( const BData& d, size_t len )
{
	if ( len >= d.second )
		return d;
	return std::make_pair( d.first, len );
}

inline BData Tail( const BData& d, size_t index )
{
	if ( index >= d.second )
		return NULL_BDATA;
	return std::make_pair( d.first + index, d.second - index );
}

class WriteableStream
{
public:
	virtual void Write( BData data ) = 0;
	virtual void EndWrite() = 0;
	virtual ~WriteableStream()
	{}
};

typedef std::function< void ( const unsigned char*, int, unsigned char*, int*  ) > TranformFunc;

const size_t batchSize = 256;
class TransformingWriter : public WriteableStream
{
public:
	TransformingWriter( WriteableStream* os, size_t blockMinSize, const TranformFunc& t )
		: m_os ( os )
		, m_blockMinSize( blockMinSize )
		, m_buf( batchSize + blockMinSize + 1 )
		, m_nextIndex( 0 )
		, m_transform( t )
	{}
	
	virtual ~TransformingWriter()
	{}

	void Write( BData data )
	{
		while ( data.first && data.second )
		{
			BData hd = data;
			size_t outBufSize = m_buf.size() > m_nextIndex ? (size_t)(m_buf.size() - m_nextIndex) : 0;
			assert( outBufSize > m_blockMinSize );
			if ( outBufSize < ( data.second + m_blockMinSize ) )
			{
				hd = Head( data, outBufSize - m_blockMinSize );
			}
			
			data = Tail( data, hd.second );
			Transform( hd );
		}
	}

	void EndWrite()
	{
		Transform( NULL_BDATA );
		if ( m_os )
		{
			if ( m_nextIndex )
				m_os->Write( std::make_pair( &m_buf.front(), (size_t) m_nextIndex ) );
			m_os->EndWrite();
		}
	}

private:
	void Transform( BData data )
	{
		int transformed = m_buf.size() - m_nextIndex;
		m_transform( data.first, data.second, &m_buf[ m_nextIndex ], &transformed );
		assert( (m_nextIndex + transformed) <= m_buf.size() );
		m_nextIndex += transformed;
		if ( ( m_buf.size() - m_nextIndex ) <= m_blockMinSize )
		{
			Flush();
		}
	}

	void Flush()
	{
		if ( m_os && m_nextIndex )
			m_os->Write( std::make_pair( &m_buf.front(), (size_t) m_nextIndex ) );
		m_nextIndex = 0;
	}

	WriteableStream* m_os;
	const size_t m_blockMinSize;
	std::vector< unsigned char > m_buf;
	int m_nextIndex;
	TranformFunc m_transform;
};

std::shared_ptr< WriteableStream > CreateCryptingWriter( const std::string& passwd, bool bEncrypt, WriteableStream* outStream );

class FdWriter : public WriteableStream
{
public:
	FdWriter( int fd )
		: m_fd( fd )
	{
	}

	void Write( BData data )
	{
		if ( m_fd && data.first && data.second ) 
		{
			ssize_t n = write( m_fd, data.first, data.second );
			//if ( n < data.second )
			{
				printf( "%zd = write( %d, %zu ) - error = %d\n", n, m_fd, data.second, errno );
			}
		}
	}

	void EndWrite()
	{
		printf("%s(%d)\n", "fsync", m_fd );
		if ( m_fd )
			fsync( m_fd );
	}
	
	int GetFd() const
	{
		return m_fd;
	}

private:
	int m_fd;
};

