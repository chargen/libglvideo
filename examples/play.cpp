#include <iostream>
#include <deque>
#include <pez.h>
#include <glew.h>
#include <iomanip>
#include <chrono>
#include <sstream>
#include "glvideo.h"
#include "Movie.h"

using namespace std;

static const std::string VERTEX_SHADER_SOURCE =
        R"EOF(
attribute vec2 Position;
attribute vec2 InCoord;
varying vec2 OutCoord;

void main()
{
    OutCoord = InCoord;
    gl_Position = vec4(Position, 0, 1);
}
)EOF";

static const std::string FRAGMENT_SHADER_SOURCE =
R"EOF(
varying vec2 OutCoord;
uniform sampler2D Sampler;

void main()
{
   gl_FragColor = texture2D(Sampler, OutCoord);
}
)EOF";

static const std::string YCoCg_FRAGMENT_SHADER_SOURCE =
        R"EOF(
varying vec2 OutCoord;
uniform sampler2D Sampler;

void main()
{
    vec4 color = texture2D(Sampler, OutCoord);
    float Co = color.x - ( 0.5 * 256.0 / 255.0 );
    float Cg = color.y - ( 0.5 * 256.0 / 255.0 );
    float Y = color.w;
    gl_FragColor = vec4( Y + Co - Cg, Y + Cg, Y - Co - Cg, color.a );
}
)EOF";


glvideo::Movie::ref movie;
// it is necessary to maintain a pointer to the current frame, so that the
// reading thread doesn't discard the frame's texture before you are ready.
glvideo::Frame::ref frame = nullptr;

static void BuildGeometry( float aspect );

static void LoadEffect( bool isYCoCg = false );

deque<unsigned int> frameTimes;
static unsigned int sumElapsedMilliseconds = 0;
typedef chrono::high_resolution_clock hrclock;
static hrclock::time_point lastReportTime = hrclock::now();

enum {
    PositionSlot, TexCoordSlot
};

void PezHandleMouse( int x, int y, int action ) {}

void PezUpdate( unsigned int elapsedMilliseconds )
{
    frameTimes.push_back( elapsedMilliseconds );
    sumElapsedMilliseconds += elapsedMilliseconds;
    while ( frameTimes.size() > 100 ) {
        sumElapsedMilliseconds -= frameTimes.front();
        frameTimes.pop_front();
    }

    auto now = hrclock::now();
    if ( chrono::duration_cast<chrono::seconds>( now - lastReportTime ).count() > 1 ) {
        double avg = (double) sumElapsedMilliseconds / (double) frameTimes.size();
        double fps = 1000.0 / avg;
        cout << "Frame AVG ms: " << setprecision( 2 ) << avg << "ms (" << fps << " fps)" << endl;
        lastReportTime = now;

		if ( movie->isPlaying() ) movie->stop();
		else movie->play();
    }
}


void checkGlError()
{
	GLenum err( glGetError() );
	while ( err != GL_NO_ERROR ) {
		string error;

		switch ( err ) {
		case GL_INVALID_OPERATION:      error = "INVALID_OPERATION";      break;
		case GL_INVALID_ENUM:           error = "INVALID_ENUM";           break;
		case GL_INVALID_VALUE:          error = "INVALID_VALUE";          break;
		case GL_OUT_OF_MEMORY:          error = "OUT_OF_MEMORY";          break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:  error = "INVALID_FRAMEBUFFER_OPERATION";  break;
		}

		cerr << "GL_" << error.c_str() << endl;
#if defined( GLVIDEO_MSW )
		OutputDebugString( ( "GL Error: GL_" + error ).c_str() );
#endif
		err = glGetError();
	}
}

void PezRender()
{
	glClear( GL_COLOR_BUFFER_BIT );

    glActiveTexture( GL_TEXTURE0 );
    frame = movie->getCurrentFrame();
    if ( frame ) {
        glBindTexture( frame->getTextureTarget(), frame->getTextureId() );
		glDrawArrays( GL_TRIANGLES, 0, 6 );
	}

	checkGlError();
}

const char *PezInitialize( int width, int height )
{
//    string filename = "/Users/ian/Desktop/MJPEG.mov";
    string filename = "/Users/ian/Desktop/Hap.mov";

    auto ctx = glvideo::GLContext::makeSharedFromCurrent();
    movie = glvideo::Movie::create( ctx, filename );


    BuildGeometry((float) width / (float) height );
	LoadEffect( movie->getCodec() == "HapY" );

    cout << "Format: " << movie->getFormat() << endl;
    cout << "Duration (seconds): " << movie->getDuration() << endl;
    cout << "Size: " << movie->getWidth() << "x" << movie->getHeight() << endl;
    cout << "Framerate: " << movie->getFramerate() << endl;
    cout << "Number of tracks: " << movie->getNumTracks() << endl;
    for ( int i = 0; i < movie->getNumTracks(); ++i ) {
        cout << "\tTrack " << i << " type: " << movie->getTrackDescription( i ) << endl;
    }

    movie->loop().play();

    return "Test Playback";
}


static void BuildGeometry( float aspect )
{
    float X = 1.f;
    float Y = 1.f;
    float verts[] = {
            -X, -Y, 0, 1,
            -X, +Y, 0, 0,
            +X, +Y, 1, 0,
            +X, +Y, 1, 0,
            +X, -Y, 1, 1,
            -X, -Y, 0, 1,
    };

    GLuint vboHandle;
    GLsizeiptr vboSize = sizeof( verts );
    GLsizei stride = 4 * sizeof( float );
    GLenum usage = GL_STATIC_DRAW;
    GLvoid *texCoordOffset = (GLvoid *) (sizeof( float ) * 2);

#if defined(GLVIDEO_MSW)
	GLuint vao;
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
#endif

    glGenBuffers( 1, &vboHandle );
	glBindBuffer( GL_ARRAY_BUFFER, vboHandle );
	glBufferData( GL_ARRAY_BUFFER, vboSize, verts, usage );
	glVertexAttribPointer( PositionSlot, 2, GL_FLOAT, GL_FALSE, stride, 0 );
	glVertexAttribPointer( TexCoordSlot, 2, GL_FLOAT, GL_FALSE, stride, texCoordOffset );
	glEnableVertexAttribArray( PositionSlot );
	glEnableVertexAttribArray( TexCoordSlot );
}

static void LoadEffect( bool isYCoCg )
{
    const char *vsSource = VERTEX_SHADER_SOURCE.c_str();
    const char *fsSource;
    if ( isYCoCg ) {
        fsSource = YCoCg_FRAGMENT_SHADER_SOURCE.c_str();
    } else {
        fsSource = FRAGMENT_SHADER_SOURCE.c_str();
    }
    GLuint vsHandle, fsHandle;
    GLint compileSuccess, linkSuccess;
    GLchar compilerSpew[256];
    GLuint programHandle;

    vsHandle = glCreateShader( GL_VERTEX_SHADER );
    fsHandle = glCreateShader( GL_FRAGMENT_SHADER );

    glShaderSource( vsHandle, 1, &vsSource, 0 );
    glCompileShader( vsHandle );
    glGetShaderiv( vsHandle, GL_COMPILE_STATUS, &compileSuccess );
    glGetShaderInfoLog( vsHandle, sizeof( compilerSpew ), 0, compilerSpew );
    PezCheckCondition( compileSuccess, compilerSpew );

    glShaderSource( fsHandle, 1, &fsSource, 0 );
    glCompileShader( fsHandle );
    glGetShaderiv( fsHandle, GL_COMPILE_STATUS, &compileSuccess );
    glGetShaderInfoLog( fsHandle, sizeof( compilerSpew ), 0, compilerSpew );
    PezCheckCondition( compileSuccess, compilerSpew );

    programHandle = glCreateProgram();
    glAttachShader( programHandle, vsHandle );
    glAttachShader( programHandle, fsHandle );
    glBindAttribLocation( programHandle, PositionSlot, "Position" );
    glBindAttribLocation( programHandle, TexCoordSlot, "InCoord" );
    glLinkProgram( programHandle );
    glGetProgramiv( programHandle, GL_LINK_STATUS, &linkSuccess );
    glGetProgramInfoLog( programHandle, sizeof( compilerSpew ), 0, compilerSpew );
    PezCheckCondition( linkSuccess, compilerSpew );

    glUseProgram( programHandle );

    GLint loc = glGetUniformLocation( programHandle, "Sampler" );
    glUniform1i( loc, 0 );
}
