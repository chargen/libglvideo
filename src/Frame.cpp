#include "Frame.h"
#include <algorithm>
#include "gl_includes.h"

using namespace glvideo;
using namespace std;

Frame::Frame( unsigned char const *const data, GLsizei imageSize, FrameTexture::Format texFormat ) :
        m_texSize( imageSize ),
        m_texFormat( texFormat )
{
    m_texData = unique_ptr< unsigned char[] >( new unsigned char[ m_texSize + 1 ] );
    copy( data, data + m_texSize, m_texData.get() );
}

void Frame::createTexture()
{
    if ( m_ftex ) return;

    m_ftex = FrameTexture::create( m_pbo, m_texSize, m_texFormat );
}

bool Frame::bufferTexture( GLuint pbo )
{
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, pbo );
    // Call glBufferDataARB to cancel any work the GPU is currently doing with
    // the PBO, to avoid glMapBufferARB blocking in the case that there is
    // pending work.
    glBufferData( GL_PIXEL_UNPACK_BUFFER, m_texSize, NULL, GL_STATIC_DRAW );

    GLubyte* buffer = (GLubyte*)glMapBuffer( GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY );

    if ( buffer ) {
        copy( m_texData.get(), m_texData.get() + m_texSize, buffer );
        glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER );
        m_pbo = pbo;
        return true;
    }

    return false;
}