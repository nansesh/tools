#include "encrypt.h"
#include <openssl/evp.h>
#include <openssl/aes.h>

#ifdef TEST
#include <iostream>
#endif

static void InitCryptContext( const std::string& passStr, EVP_CIPHER_CTX& ctx, bool bEncrypt )
{
	int nrounds = 5;
	unsigned char key[32], iv[32];
	  
	/*
	* Gen key & IV for AES 256 CBC mode. A SHA1 digest is used to hash the supplied key material.
	* nrounds is the number of times the we hash the material. More rounds are more secure but
	* slower.
	*/
	unsigned int salt[] = {12345, 54321};
	int i = EVP_BytesToKey( EVP_aes_256_cbc(), EVP_sha1(), (unsigned char*)( &salt[0] ), (unsigned char*)( passStr.c_str() ), passStr.size(), nrounds, key, iv );
	if (i != 32) 
	{
	    throw std::runtime_error( "could not generate sufficient key length" );
	}

	EVP_CIPHER_CTX_init( &ctx );
	if ( bEncrypt )
		EVP_EncryptInit_ex( &ctx, EVP_aes_256_cbc(), NULL, key, iv );
	else
		EVP_DecryptInit_ex( &ctx, EVP_aes_256_cbc(), NULL, key, iv );
}

class CryptingWriter : public TransformingWriter
{
public:
	CryptingWriter( WriteableStream* os, const std::string& key, bool bEncrypt );
	~CryptingWriter();

private:
	EVP_CIPHER_CTX m_ctx;
	void ETransform( const unsigned char* in, int len, unsigned char* out, int* outLen );
	void DTransform( const unsigned char* in, int len, unsigned char* out, int* outLen );
};

CryptingWriter::CryptingWriter( WriteableStream* os, const std::string& key, bool bEncrypt )
		: TransformingWriter( os, 
			AES_BLOCK_SIZE, 
			std::bind( bEncrypt ? &CryptingWriter::ETransform : &CryptingWriter::DTransform, this, 
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4 ) )
{
	InitCryptContext( key, m_ctx, bEncrypt );
}

CryptingWriter::~CryptingWriter()
{
	EVP_CIPHER_CTX_cleanup( &m_ctx );
}

void CryptingWriter::ETransform( const unsigned char* in, int len, unsigned char* out, int* outLen )
{
	if ( in && len )
	{
		//std::cerr << "ETransform : inlen = " << len << " outlen = " << *outLen << std::endl;
		if ( !EVP_EncryptUpdate( &m_ctx, out, outLen, in, len ) )
			throw std::runtime_error( "EVP_EncryptUpdate failed" );
		//std::cerr << "ETransform : post transform outlen = " << *outLen << std::endl;
	}
	else
	{
		//std::cerr << "ETransform finalize " << " outlen = " << *outLen << std::endl;
		if ( !EVP_EncryptFinal_ex( &m_ctx, out, outLen ) )
			throw std::runtime_error( "EVP_EncryptFinal_ex failed" );
		//std::cerr << "ETransform finalize : post transform outlen = " << *outLen << std::endl;
	}
}

void CryptingWriter::DTransform( const unsigned char* in, int len, unsigned char* out, int* outLen )
{
	if ( in && len )
	{
		if ( !EVP_DecryptUpdate( &m_ctx, out, outLen, in, len ) )
			throw std::runtime_error( "EVP_DecryptUpdate failed" );
	}
	else
	{
		if ( !EVP_DecryptFinal_ex( &m_ctx, out, outLen ) )
			throw std::runtime_error( "EVP_DecryptFinal_ex failed" );
	}
}

std::shared_ptr< WriteableStream > CreateCryptingWriter( const std::string& passwd, bool bEncrypt, WriteableStream* outStream )
{
	return std::shared_ptr< WriteableStream > ( new CryptingWriter( outStream, passwd, bEncrypt ) );
}

#ifdef TEST
void Print( BData d )
{
	for ( size_t i = 0; i < d.second; ++i )
		std::cout << std::hex << (unsigned int)d.first[ i ] << ", ";
	std::cout << std::endl;
}

int main( int argc, char* argv[] )
{
	if ( argc < 2 )
	{
		std::cout << "usage : " << argv[ 0 ] << " <password to use for encryption> " << std::endl;
		return 1;
	}

	std::string pass = argv[ 1 ];

	bool bEncrpyt = true;
	if ( argc > 2 )
	{
		bEncrpyt = std::string( "dec" ) != argv[ 2 ];
	}

	try
	{
		FdWriter sw( STDOUT_FILENO );
		CryptingWriter cw( &sw, pass, bEncrpyt );

		unsigned char buf[512];
		ssize_t n = 0;
		while ( ( n = read( STDIN_FILENO, buf, sizeof(buf) ) ) > 0 )
		{
			cw.Write( std::make_pair( buf, (size_t)n ) );
		}
		cw.EndWrite();

		//std::cerr << "number of bytes written = " << sw.byteCount << std::endl;
	}
	catch ( std::exception& e )
	{
		std::cerr << "Caught exception - message = " << ( e.what() ? e.what() : "" ) << std::endl;
	}
	return 0;
}
#endif

