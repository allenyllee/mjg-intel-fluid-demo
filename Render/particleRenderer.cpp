/*! \file particleRenderer.cpp

    \brief Class to render particles

    \see Accompanying articles for more information:
        http://software.intel.com/en-us/articles/fluid-simulation-for-video-games-part-1/

    \author Copyright 2009 Dr. Michael Jason Gourlay; All rights reserved.
*/

#include <math.h>
#include <limits.h>

#include <xmmintrin.h>

#if defined( WIN32 )
    #include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#include "glut.h"
#include "gl_ext.h"

#include "Core/Math/Mat4.h"
#include "Core/Performance/perf.h"

#include "particleRenderer.h"

#define SHUFFLE_BITS( i )     ( ( (i) & 0x80000000 ) >>  4 ) \
                            | ( ( (i) & 0x08000000 ) >> 14 ) \
                            | ( ( (i) & 0x00800000 ) >> 20 ) \
                            | ( ( (i) & 0x00100000 ) << 11 ) \
                            | ( ( (i) & 0x00040000 ) >> 10 ) \
                            | ( ( (i) & 0x00020000 ) >>  5 ) \
                            | ( ( (i) & 0x00004000 ) <<  4 ) \
                            | ( ( (i) & 0x00002000 ) >> 12 ) \
                            | ( ( (i) & 0x00001000 ) >>  2 ) \
                            | ( ( (i) & 0x00000400 ) << 13 ) \
                            | ( ( (i) & 0x00000100 ) >>  8 ) \
                            | ( ( (i) & 0x00000040 ) << 11 ) \
                            | ( ( (i) & 0x00000008 ) >>  1 ) \
                            | ( ( (i) & 0x00000004 ) << 12 ) \
                            | ( ( (i) & 0x00000002 ) <<  5 ) \
                            | ( ( (i) & 0x00000001 ) << 20 )




struct VertexFormatPositionNormalTexture
{   // Custom vertex format for position+normal+texture coordinates
    float tu, tv ;      // 2D texture coordinates
    float nx, ny, nz ;  // surface normal unit vector
    float px, py, pz ;  // untransformed (world-space) position
} ;
#define VertexFormatFlags_NormalTexture (GL_T2F_N3F_V3F)

struct VertexFormatPos3Tex2
{   // Custom vertex format for position+texture coordinates
    float tu , tv ;             ///< texture coordinates
    float px , py , pz ;        ///< untransformed (world-space) position
} ;
#define VertexFormatFlags_Pos3Tex2  (GL_T2F_V3F)

struct VertexFormatPos4Tex4
{   // Custom vertex format for position+texture (special)
    float tu , tv , ts , tt ;   ///< particle orientation (xyz), particle size and vertex index (w=size+index*shift)
    float px , py , pz , pw ;   ///< untransformed (world-space) position (xyz) and birth time (w)
} ;
#define VertexFormatFlags_Pos4Tex4  (GL_T4F_V4F)




#if USE_SEPARATE_VBOS

struct VertexFormatPos3
{   // Custom vertex format for position only.
    float px , py , pz ;   ///< untransformed (world-space) position (xyz)
} ;
#define VertexFormatFlags_Pos3 (GL_V3F)

struct VertexFormatTex2
{   // Custom vertex format for texture coordinates.
    // This format has no GL_* macro; it is used as a separate array.
    // Separating texture coordinates lets us use the fixed function
    // pipeline without having to refill tex coords each frame.
    float tu , tv ;             ///< texture coordinates
} ;

#endif




// VBO Extension Function Pointers
PFN_GL_GENBUFFERSARB_PROC       glGenBuffers    = 0 ;   // VBO Name Generation Procedure
PFN_GL_MAPBUFFERARB_PROC        glMapBuffer     = 0 ;   // VBO Map Buffer procedure
PFN_GL_UNMAPBUFFERARB_PROC      glUnmapBuffer   = 0 ;   // VBO Unmap Buffer procedure
PFN_GL_BINDBUFFERARB_PROC       glBindBuffer    = 0 ;   // VBO Bind Procedure
PFN_GL_BUFFERDATAARB_PROC       glBufferData    = 0 ;   // VBO Data Loading Procedure
PFN_GL_DELETEBUFFERSARB_PROC    glDeleteBuffers = 0 ;   // VBO Deletion Procedure

bool gExtensionsInitialized =   false ; // ARB Extention data initialized?
bool gVboSupported          =   false ; // ARB_vertex_buffer_object supported?


#if USE_TBB
    extern unsigned gNumberOfProcessors ;

    /*! \brief Function object to fill a vertex buffer using Threading Building Blocks
    */
    class ParticleRenderer_FillVertexBuffer_TBB
    {
            ParticleRenderer * mParticleRenderer ;    ///< Address of ParticleRenderer object
            const double     & mTimeNow          ;
            const Mat4       & mViewMatrix       ;
        public:
            void operator() ( const tbb::blocked_range<size_t> & r ) const
            {   // Advect subset of tracers.
                mParticleRenderer->FillVertexBufferSlice( mTimeNow , mViewMatrix , r.begin() , r.end() ) ;
            }
            ParticleRenderer_FillVertexBuffer_TBB( ParticleRenderer * pParticleRenderer , const double & timeNow , const Mat4 & viewMatrix )
                : mParticleRenderer( pParticleRenderer )
                , mTimeNow( timeNow )
                , mViewMatrix( viewMatrix )
            {}
    } ;
#endif




#if USE_VERTEX_BUFFER_OBJECT
static bool IsExtensionSupported( char* szTargetExtension )
{
	const unsigned char *pszExtensions = NULL;
	const unsigned char *pszStart;
	unsigned char *pszWhere, *pszTerminator;

	// Extension names should not have spaces
	pszWhere = (unsigned char *) strchr( szTargetExtension, ' ' );
	if( pszWhere || *szTargetExtension == '\0' )
		return false;

	// Get Extensions String
	pszExtensions = glGetString( GL_EXTENSIONS );

	// Search The Extensions String For An Exact Copy
	pszStart = pszExtensions;
	for(;;)
	{
		pszWhere = (unsigned char *) strstr( (const char *) pszStart, szTargetExtension );
		if( !pszWhere )
			break;
		pszTerminator = pszWhere + strlen( szTargetExtension );
		if( pszWhere == pszStart || *( pszWhere - 1 ) == ' ' )
			if( *pszTerminator == ' ' || *pszTerminator == '\0' )
				return true;
		pszStart = pszTerminator;
	}
	return false;
}
#endif




/*! \brief Construct object to render particles
*/
ParticleRenderer::ParticleRenderer( const char * pParticleData , size_t stride , size_t offsetToAngVel , size_t offsetToSize )
    : mParticleData( pParticleData )
    , mStride( stride )
    , mOffsetToAngVel( offsetToAngVel )
    , mOffsetToSize( offsetToSize )
    , mVertexBuffer( 0 )
    , mVertexBufferCapacity( 0 )
    , mIndices( 0 )
    , mIndicesCapacity( 0 )
#if USE_SEPARATE_VBOS
    , mVertexBufferGrew( true )
#endif
{
#if USE_VERTEX_BUFFER_OBJECT
    memset( mVboNames , 0 , sizeof( mVboNames ) ) ;
#endif
}




/*! \brief Destruct object to render vortex particles
*/
ParticleRenderer::~ParticleRenderer()
{
#if USE_VERTEX_BUFFER_OBJECT
    if( gVboSupported )
    {   // Used new-style vertex buffer objects.
        // Tell OpenGL to delete vertex buffer object allocated in "GPU domain"
        glDeleteBuffers( NUM_VBO_MAX , mVboNames ) ;
    }
    else
#endif
    {   // Used old-style vertex arrays (not vertex buffer objects).
        delete[] mVertexBuffer ;    // Delete buffer allocated by CPU
    }

    mVertexBuffer           = 0 ;
    mVertexBufferCapacity   = 0 ;
    if( mIndices != 0 )
    {
        free( mIndices ) ;
        mIndices = 0 ;
        mIndicesCapacity = 0 ;
    }
}




#if USE_FANCY_PARTICLES
static int CompareParticles( const void * p0 , const void * p1 )
{
    ParticleRenderer::ParticleIndex * pi0 = (ParticleRenderer::ParticleIndex *) p0 ;
    ParticleRenderer::ParticleIndex * pi1 = (ParticleRenderer::ParticleIndex *) p1 ;
    if ( pi0->mDepth > pi1->mDepth )
    {
        return 1 ;
    }
    else if ( pi0->mDepth < pi1->mDepth )
    {
        return -1 ;
    }
    else
    {
        return 0 ;
    }
}
#endif




/*! \brief Render vortex particles

    \param timeNow -- current virtual time

    \param timeChange - change in virtual time since last call to Render

*/
void ParticleRenderer::FillVertexBufferSlice( const double & timeNow , const Mat4 & viewMatrix , size_t iPclStart , size_t iPclEnd )
{
    static const unsigned   nvpp                = 4 ; // number of vertices per particle

    // Extract world space direction vectors associated with view (used to compute camera-facing coordinates).
    // Note that these vectors are the unit vectors of the inverse of the view matrix.
    // They are the world-space unit vectors of the view transformation.
    Vec3 viewRight  ( viewMatrix.m[0][0] , viewMatrix.m[1][0] , viewMatrix.m[2][0] ) ;
    Vec3 viewUp     ( viewMatrix.m[0][1] , viewMatrix.m[1][1] , viewMatrix.m[2][1] ) ;
    Vec3 viewForward( viewMatrix.m[0][2] , viewMatrix.m[1][2] , viewMatrix.m[2][2] ) ;

#if USE_FANCY_PARTICLES
    static bool bSort = true ;
    int iSortedWeight = 1 ;
    int iOtherWeight  = 0 ;

    if( bSort )
    {
        iSortedWeight = 1 ;
        iOtherWeight  = 0 ;
    }
    else
    {
        iSortedWeight = 0 ;
        iOtherWeight  = 1 ;
    }

    // Fill vertex buffer with "fancy" particles with normals
    VertexFormatPositionNormalTexture * pVertices = ( VertexFormatPositionNormalTexture * ) mVertexBuffer ;
    for( size_t iPcl = iPclStart ; iPcl < iPclEnd ; ++ iPcl )
    {   // For each particle in this slice...
        const char *    pPos        = mParticleData + mIndices[ iPcl ].mPcl * mStride ;
        const char *    pAngVel     = pPos + mOffsetToAngVel ;
        const char *    pSize       = pPos + mOffsetToSize ;
        const Vec3  &   pclPos      = * ( (Vec3*) pPos ) ;
        const Vec3  &   pclAngVel   = * ( (Vec3*) pAngVel ) ;
        static const float oneOverUintMax = 1.0f / float( UINT_MAX ) ;
        const float     fPhase      = TWO_PI * float( SHUFFLE_BITS( iPcl ) ) * oneOverUintMax ;
        const float &   pclAngle    = ( pclAngVel * timeNow ).Magnitude() + fPhase ;
        const float     cosAngle    = cos( pclAngle ) ;
        const float     sinAngle    = sin( pclAngle ) ;
        const float &   rSize       = * ( (float*) pSize ) ;
        Vec4            pclRight    = (   viewRight * cosAngle + viewUp * sinAngle ) * rSize ;
        Vec4            pclUp       = ( - viewRight * sinAngle + viewUp * cosAngle ) * rSize ;

        static const float fraction = 0.1f ;

        // Assign positions and texture coordinates for each vertex of the quadrilateral.
        pVertices[ iPcl*nvpp   ].tu = 1.0f ;
        pVertices[ iPcl*nvpp   ].tv = 0.0f ;
        pVertices[ iPcl*nvpp   ].nx = -( pclRight.x + pclUp.x ) - fraction * viewForward.x ;
        pVertices[ iPcl*nvpp   ].ny = -( pclRight.y + pclUp.y ) - fraction * viewForward.y ;
        pVertices[ iPcl*nvpp   ].nz = -( pclRight.z + pclUp.z ) - fraction * viewForward.z ;
        ((Vec3*) (&pVertices[ iPcl*nvpp   ].nx))->Normalize() ;
        pVertices[ iPcl*nvpp   ].px = ( pclPos.x + pclRight.x + pclUp.x ) ;
        pVertices[ iPcl*nvpp   ].py = ( pclPos.y + pclRight.y + pclUp.y ) ;
        pVertices[ iPcl*nvpp   ].pz = ( pclPos.z + pclRight.z + pclUp.z ) ;

        pVertices[ iPcl*nvpp+1 ].tu = 0.0f ;
        pVertices[ iPcl*nvpp+1 ].tv = 0.0f ;
        pVertices[ iPcl*nvpp+1 ].nx = -( - pclRight.x + pclUp.x ) - fraction * viewForward.x ;
        pVertices[ iPcl*nvpp+1 ].ny = -( - pclRight.y + pclUp.y ) - fraction * viewForward.y ;
        pVertices[ iPcl*nvpp+1 ].nz = -( - pclRight.z + pclUp.z ) - fraction * viewForward.z ;
        ((Vec3*) (&pVertices[ iPcl*nvpp+1 ].nx))->Normalize() ;
        pVertices[ iPcl*nvpp+1 ].px = ( pclPos.x - pclRight.x + pclUp.x ) ;
        pVertices[ iPcl*nvpp+1 ].py = ( pclPos.y - pclRight.y + pclUp.y ) ;
        pVertices[ iPcl*nvpp+1 ].pz = ( pclPos.z - pclRight.z + pclUp.z ) ;

        pVertices[ iPcl*nvpp+2 ].tu = 0.0f ;
        pVertices[ iPcl*nvpp+2 ].tv = 1.0f ;
        pVertices[ iPcl*nvpp+2 ].nx = -( - pclRight.x - pclUp.x ) - fraction * viewForward.x ;
        pVertices[ iPcl*nvpp+2 ].ny = -( - pclRight.y - pclUp.y ) - fraction * viewForward.y ;
        pVertices[ iPcl*nvpp+2 ].nz = -( - pclRight.z - pclUp.z ) - fraction * viewForward.z ;
        ((Vec3*) (&pVertices[ iPcl*nvpp+2 ].nx))->Normalize() ;
        pVertices[ iPcl*nvpp+2 ].px = ( pclPos.x - pclRight.x - pclUp.x ) ;
        pVertices[ iPcl*nvpp+2 ].py = ( pclPos.y - pclRight.y - pclUp.y ) ;
        pVertices[ iPcl*nvpp+2 ].pz = ( pclPos.z - pclRight.z - pclUp.z ) ;

        pVertices[ iPcl*nvpp+3 ].tu = 1.0f ;
        pVertices[ iPcl*nvpp+3 ].tv = 1.0f ;
        pVertices[ iPcl*nvpp+3 ].nx = -( pclRight.x - pclUp.x ) - fraction * viewForward.x ;
        pVertices[ iPcl*nvpp+3 ].ny = -( pclRight.y - pclUp.y ) - fraction * viewForward.y ;
        pVertices[ iPcl*nvpp+3 ].nz = -( pclRight.z - pclUp.z ) - fraction * viewForward.z ;
        ((Vec3*) (&pVertices[ iPcl*nvpp+3 ].nx))->Normalize() ;
        pVertices[ iPcl*nvpp+3 ].px = ( pclPos.x + pclRight.x - pclUp.x ) ;
        pVertices[ iPcl*nvpp+3 ].py = ( pclPos.y + pclRight.y - pclUp.y ) ;
        pVertices[ iPcl*nvpp+3 ].pz = ( pclPos.z + pclRight.z - pclUp.z ) ;
    }

#else // Fill vertex buffer with "plain" particles without normals

    #if USE_SEPARATE_VBOS
    if( gVboSupported )
    {
        if( mVertexBufferGrew )
        {   // Vertex buffer grew so we need more texture coordinates.
            VertexFormatTex2 * pVertTC  = ( VertexFormatTex2 * ) mTexCoordBuffer ;
            for( size_t iPcl = iPclStart ; iPcl < iPclEnd ; ++ iPcl )
            {   // For each particle in this slice...
                // Assign texture coordinates for each vertex of the quadrilateral.
                // These can be reused each frame, as long as the vertex buffer does not grow.
                pVertTC[ iPcl*nvpp   ].tu = 1.0f ;
                pVertTC[ iPcl*nvpp   ].tv = 0.0f ;

                pVertTC[ iPcl*nvpp+1 ].tu = 0.0f ;
                pVertTC[ iPcl*nvpp+1 ].tv = 0.0f ;

                pVertTC[ iPcl*nvpp+2 ].tu = 0.0f ;
                pVertTC[ iPcl*nvpp+2 ].tv = 1.0f ;

                pVertTC[ iPcl*nvpp+3 ].tu = 1.0f ;
                pVertTC[ iPcl*nvpp+3 ].tv = 1.0f ;
            }
        }

        VertexFormatPos3 * pVertPos = ( VertexFormatPos3 * ) mVertexBuffer  ;
        for( size_t iPcl = iPclStart ; iPcl < iPclEnd ; ++ iPcl )
        {   // For each particle in this slice...
            // Obtain information about particle position, size and orientation.
            const char *    pPos        = mParticleData + iPcl * mStride ;
            const Vec3  &   pclPos      = * ( (Vec3*) pPos ) ;
            const char *    pSize       = pPos + mOffsetToSize ;
            const char *    pAngVel     = pPos + mOffsetToAngVel ;
            const Vec3  &   pclAngVel   = * ( (Vec3*) pAngVel ) ;
            const float &   pclAngle    = ( pclAngVel * timeNow ).Magnitude() ;
            const float     cosAngle    = cos( pclAngle ) ;
            const float     sinAngle    = sin( pclAngle ) ;
            const float &   rSize       = * ( (float*) pSize ) ;
            Vec3            pclRight    = (   viewRight * cosAngle + viewUp * sinAngle ) * rSize ;
            Vec3            pclUp       = ( - viewRight * sinAngle + viewUp * cosAngle ) * rSize ;

            // Assign positions for each vertex of the quadrilateral.
            pVertPos[ iPcl*nvpp   ].px = ( pclPos.x + pclRight.x + pclUp.x ) ;
            pVertPos[ iPcl*nvpp   ].py = ( pclPos.y + pclRight.y + pclUp.y ) ;
            pVertPos[ iPcl*nvpp   ].pz = ( pclPos.z + pclRight.z + pclUp.z ) ;

            pVertPos[ iPcl*nvpp+1 ].px = ( pclPos.x - pclRight.x + pclUp.x ) ;
            pVertPos[ iPcl*nvpp+1 ].py = ( pclPos.y - pclRight.y + pclUp.y ) ;
            pVertPos[ iPcl*nvpp+1 ].pz = ( pclPos.z - pclRight.z + pclUp.z ) ;

            pVertPos[ iPcl*nvpp+2 ].px = ( pclPos.x - pclRight.x - pclUp.x ) ;
            pVertPos[ iPcl*nvpp+2 ].py = ( pclPos.y - pclRight.y - pclUp.y ) ;
            pVertPos[ iPcl*nvpp+2 ].pz = ( pclPos.z - pclRight.z - pclUp.z ) ;

            pVertPos[ iPcl*nvpp+3 ].px = ( pclPos.x + pclRight.x - pclUp.x ) ;
            pVertPos[ iPcl*nvpp+3 ].py = ( pclPos.y + pclRight.y - pclUp.y ) ;
            pVertPos[ iPcl*nvpp+3 ].pz = ( pclPos.z + pclRight.z - pclUp.z ) ;
        }
    }
    else
    #endif
    {

        // Fill vertex buffer with "plain" particles without normals
        VertexFormatPos3Tex2 * pVertices = ( VertexFormatPos3Tex2 * ) mVertexBuffer ;
        for( size_t iPcl = iPclStart ; iPcl < iPclEnd ; ++ iPcl )
        {   // For each particle in this slice...
            const char *    pPos        = mParticleData + iPcl * mStride ;
            const Vec3  &   pclPos      = * ( (Vec3*) pPos ) ;
            const char *    pSize       = pPos + mOffsetToSize ;
            const char *    pAngVel     = pPos + mOffsetToAngVel ;
            const Vec3  &   pclAngVel   = * ( (Vec3*) pAngVel ) ;
            const float &   pclAngle    = ( pclAngVel * timeNow ).Magnitude() ;
            const float     cosAngle    = cos( pclAngle ) ;
            const float     sinAngle    = sin( pclAngle ) ;
            const float &   rSize       = * ( (float*) pSize ) ;
            Vec3            pclRight    = (   viewRight * cosAngle + viewUp * sinAngle ) * rSize ;
            Vec3            pclUp       = ( - viewRight * sinAngle + viewUp * cosAngle ) * rSize ;

            // Assign positions and texture coordinates for each vertex of the quadrilateral.
            pVertices[ iPcl*nvpp   ].tu = 1.0f ;
            pVertices[ iPcl*nvpp   ].tv = 0.0f ;
            pVertices[ iPcl*nvpp   ].px = ( pclPos.x + pclRight.x + pclUp.x ) ;
            pVertices[ iPcl*nvpp   ].py = ( pclPos.y + pclRight.y + pclUp.y ) ;
            pVertices[ iPcl*nvpp   ].pz = ( pclPos.z + pclRight.z + pclUp.z ) ;

            pVertices[ iPcl*nvpp+1 ].tu = 0.0f ;
            pVertices[ iPcl*nvpp+1 ].tv = 0.0f ;
            pVertices[ iPcl*nvpp+1 ].px = ( pclPos.x - pclRight.x + pclUp.x ) ;
            pVertices[ iPcl*nvpp+1 ].py = ( pclPos.y - pclRight.y + pclUp.y ) ;
            pVertices[ iPcl*nvpp+1 ].pz = ( pclPos.z - pclRight.z + pclUp.z ) ;

            pVertices[ iPcl*nvpp+2 ].tu = 0.0f ;
            pVertices[ iPcl*nvpp+2 ].tv = 1.0f ;
            pVertices[ iPcl*nvpp+2 ].px = ( pclPos.x - pclRight.x - pclUp.x ) ;
            pVertices[ iPcl*nvpp+2 ].py = ( pclPos.y - pclRight.y - pclUp.y ) ;
            pVertices[ iPcl*nvpp+2 ].pz = ( pclPos.z - pclRight.z - pclUp.z ) ;

            pVertices[ iPcl*nvpp+3 ].tu = 1.0f ;
            pVertices[ iPcl*nvpp+3 ].tv = 1.0f ;
            pVertices[ iPcl*nvpp+3 ].px = ( pclPos.x + pclRight.x - pclUp.x ) ;
            pVertices[ iPcl*nvpp+3 ].py = ( pclPos.y + pclRight.y - pclUp.y ) ;
            pVertices[ iPcl*nvpp+3 ].pz = ( pclPos.z + pclRight.z - pclUp.z ) ;
        }
    }
#endif
}




/*! \brief Render vortex particles

    \param timeNow -- current virtual time

    \param timeChange - change in virtual time since last call to Render

*/
void ParticleRenderer::Render( double timeNow , float timeChange , size_t numParticles )
{
    QUERY_PERFORMANCE_ENTER ;

    if( 0 == numParticles )
    {   // No particles to render so do nothing
        return ;
    }

#if USE_FANCY_PARTICLES
    unsigned                vertexFormatFlags   = VertexFormatFlags_NormalTexture ;
    unsigned                vertexFormatSize    = sizeof( VertexFormatPositionNormalTexture ) ;
#else
    unsigned                vertexFormatFlags   = VertexFormatFlags_Pos3Tex2 ;
    unsigned                vertexFormatSize    = sizeof( VertexFormatPos3Tex2 ) ;
#endif
    static const unsigned   nvpp                = 4 ; // number of vertices per particle
    const size_t            numVertices         = numParticles * nvpp ;

#if USE_VERTEX_BUFFER_OBJECT
    if( ! gExtensionsInitialized )
    {   // VBO extention data has not yet been initialized
	    gVboSupported = IsExtensionSupported( "GL_ARB_vertex_buffer_object" ) ;
	    if( gVboSupported )
	    {   // This machine supports Vertex Buffer Objects.
		    // Get addresses of GL VBO functions.
		    glGenBuffers    = (PFN_GL_GENBUFFERSARB_PROC   ) wglGetProcAddress( "glGenBuffers"    );
		    glMapBuffer     = (PFN_GL_MAPBUFFERARB_PROC    ) wglGetProcAddress( "glMapBuffer"     );
		    glUnmapBuffer   = (PFN_GL_UNMAPBUFFERARB_PROC  ) wglGetProcAddress( "glUnmapBuffer"   );
		    glBindBuffer    = (PFN_GL_BINDBUFFERARB_PROC   ) wglGetProcAddress( "glBindBuffer"    );
		    glBufferData    = (PFN_GL_BUFFERDATAARB_PROC   ) wglGetProcAddress( "glBufferData"    );
		    glDeleteBuffers = (PFN_GL_DELETEBUFFERSARB_PROC) wglGetProcAddress( "glDeleteBuffers" );
	    }
        gExtensionsInitialized = true ;
    }

    if( gVboSupported )
    {
        #if USE_SEPARATE_VBOS
            // When using separate VBO's, allocate position and texCoord buffers separately.
            vertexFormatSize    = sizeof( VertexFormatPos3 ) ;
            vertexFormatFlags   = GL_V3F ; // This is actually irrelevant, but I set it anyway.
        #endif

        if(     ( mVboNames[ 0 ] != 0 )                     // vertex buffer object already exists...
            &&  ( mVertexBufferCapacity < numVertices ) )   // new size needs more space than old buffer has
        {   // Free previous vertex buffer object in preparation for creating a new one.
            glDeleteBuffers( 1 , & mVboNames[ 0 ] ) ;
            mVboNames[ 0 ] = 0 ;
        #if USE_SEPARATE_VBOS
            glDeleteBuffers( 1 , & mVboNames[ 1 ] ) ;   // Delete texCoord VBO
            mVboNames[ 1 ]      = 0     ;               // Forget old texCoord VBO "name".
        #endif
        }

        if( 0 == mVboNames[ 0 ] )
        {   // Vertex buffer object is not allocated.
            // Reserve VBO buffer identifier
            glGenBuffers( 1 , & mVboNames[ 0 ] ) ;
            mVertexBufferCapacity = numVertices ;
            // Create vertex buffer.  This allocates enough memory to hold all our vertex data.
            glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 0 ] ) ;
            const unsigned uVboSize = vertexFormatSize * numVertices ;
            glBufferData( GL_ARRAY_BUFFER , uVboSize , NULL , GL_STREAM_DRAW ) ;

        #if USE_SEPARATE_VBOS
            // Reserve VBO buffer identifier for texCoords
            glGenBuffers( 1 , & mVboNames[ 1 ] ) ;
            // Create vertex buffer for texCoords.  This allocates memory that the GPU can use directly.
            glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 1 ] ) ;
            {
                const unsigned uVboSize = (unsigned) sizeof( VertexFormatTex2 ) * numVertices ;
                glBufferData( GL_ARRAY_BUFFER , uVboSize , NULL , GL_STREAM_DRAW ) ;
            }
            mTexCoordBuffer     = (VERTEX_BUFFER_POINTER_TYPE) glMapBuffer( GL_ARRAY_BUFFER , GL_WRITE_ONLY ) ;
            mVertexBufferGrew   = true  ;   // Vertex buffer needs more texture coordinates
        }
        else
        {
            mVertexBufferGrew   = false ;   // Existing texture coordinate buffer is still useful
        #endif
        }

        glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 0 ] ) ;
        mVertexBuffer = (VERTEX_BUFFER_POINTER_TYPE) glMapBuffer( GL_ARRAY_BUFFER , GL_WRITE_ONLY ) ;
    }
    else
#endif
    {   // Use old-style vertex arrays (not vertex buffer objects)
        if(     ( mVertexBuffer != 0 )                      // vertex buffer already exists...
            &&  ( mVertexBufferCapacity < numVertices ) )   // new size needs more space than old buffer has
        {   // Free previous vertex buffer in preparation for creating a new one.
            delete[] mVertexBuffer ;
            mVertexBuffer           = 0 ;
            mVertexBufferCapacity   = 0 ;
        }

        if( 0 == mVertexBuffer )
        {   // Vertex buffer is not allocated.
            // Create vertex buffer.  This allocates enough memory to hold all our vertex data.
            // Also specify vertex format, so device knows what data vertex buffer contains.
            mVertexBuffer = new unsigned char[ vertexFormatSize * numVertices ] ;
            mVertexBufferCapacity = numVertices ;
        }
    }

    Mat4 viewMatrix ;
    glGetFloatv( GL_MODELVIEW_MATRIX , (GLfloat *) & viewMatrix ) ;

    // Extract world space direction vectors associated with view (used to compute camera-facing coordinates).
    // Note that these vectors are the unit vectors of the inverse of the view matrix.
    // They are the world-space unit vectors of the view transformation.
    Vec3 viewRight  ( viewMatrix.m[0][0] , viewMatrix.m[1][0] , viewMatrix.m[2][0] ) ;
    Vec3 viewUp     ( viewMatrix.m[0][1] , viewMatrix.m[1][1] , viewMatrix.m[2][1] ) ;
    Vec3 viewForward( viewMatrix.m[0][2] , viewMatrix.m[1][2] , viewMatrix.m[2][2] ) ;

    if( numParticles > mIndicesCapacity )
    {   // Need to allocate more space for particle index map
        if( mIndices != 0 )
        {   // Index map already exists so delete it first
            free( mIndices ) ;
        }
        mIndices    = (ParticleIndex *) malloc( sizeof( ParticleIndex ) * numParticles ) ;
        mIndicesCapacity = numParticles ;
    }

#if USE_FANCY_PARTICLES
    static bool bSort = true ;
    if( bSort )
    {
        QUERY_PERFORMANCE_ENTER ;

        for( unsigned iPcl = 0 ; iPcl < numParticles ; ++ iPcl )
        {   // For each particle...
            const char *    pPos    = mParticleData + iPcl * mStride ;
            const Vec3   &  pclPos  = * ( (Vec3*) pPos) ;
            // Assign particle index map values.
            // Later these will be sorted and therefore by proxy so will the particles
            mIndices[ iPcl ].mPcl   = iPcl ;
            mIndices[ iPcl ].mDepth = pclPos * viewForward ;
        }
        qsort( mIndices , numParticles , sizeof( ParticleIndex ) , CompareParticles ) ;

        QUERY_PERFORMANCE_EXIT( ParticlesRender_Sort ) ;
    }
#endif

    // Fill vertex buffer with geometric primitives (quadrilaterals)
    QUERY_PERFORMANCE_ENTER ;

#if USE_TBB
    // Estimate grain size based on size of problem and number of processors.
    const size_t grainSize =  MAX2( 1 , numParticles / gNumberOfProcessors ) ;
    // Fill vertex buffer using threading building blocks
    parallel_for( tbb::blocked_range<size_t>( 0 , numParticles , grainSize ) , ParticleRenderer_FillVertexBuffer_TBB( this , timeNow , viewMatrix ) ) ;
#else
    FillVertexBufferSlice( timeNow , viewMatrix , 0 , numParticles ) ;
#endif
    QUERY_PERFORMANCE_EXIT( ParticlesRender_FillVertexBuffer ) ;

    // Render particles
    QUERY_PERFORMANCE_ENTER ;

    // Inform OpenGL where vertex data resides and what its layout is.
#if USE_VERTEX_BUFFER_OBJECT
    if( gVboSupported )
    {   // Use new-style vertex buffer objects (VBO's)
        #if USE_FANCY_PARTICLES
        {
            VertexFormatPositionNormalTexture vertBufferProxy ;
            static const GLubyte * offsetTu = (GLubyte*) OFFSET_OF_MEMBER( vertBufferProxy , tu ) ;
            static const GLubyte * offsetNx = (GLubyte*) OFFSET_OF_MEMBER( vertBufferProxy , nx ) ;
            static const GLubyte * offsetPx = (GLubyte*) OFFSET_OF_MEMBER( vertBufferProxy , px ) ;
            glTexCoordPointer( 2 , GL_FLOAT , vertexFormatSize , offsetTu ) ;
            glNormalPointer  ( 3 , GL_FLOAT , vertexFormatSize , offsetNx ) ;
            glVertexPointer  ( 3 , GL_FLOAT , vertexFormatSize , offsetPx ) ;
        }
        #elif USE_SEPARATE_VBOS
        {
            static const GLubyte * offsetTu = (GLubyte*) 0 ;
            static const GLubyte * offsetPx = (GLubyte*) 0 ;
            glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 1 ] ) ;
            glUnmapBuffer( GL_ARRAY_BUFFER ) ;
            glTexCoordPointer( 2 , GL_FLOAT , sizeof( VertexFormatTex2 ) , offsetTu ) ;

            glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 0 ] ) ;
            glUnmapBuffer( GL_ARRAY_BUFFER ) ;
            glVertexPointer  ( 3 , GL_FLOAT , vertexFormatSize , offsetPx ) ;
        }
        #else
        {
            VertexFormatPos3Tex2 vertBufferProxy ;
            static const GLubyte * offsetTu = (GLubyte*) OFFSET_OF_MEMBER( vertBufferProxy , tu ) ;
            static const GLubyte * offsetPx = (GLubyte*) OFFSET_OF_MEMBER( vertBufferProxy , px ) ;
            glBindBuffer( GL_ARRAY_BUFFER , mVboNames[ 0 ] ) ;
            glUnmapBuffer( GL_ARRAY_BUFFER ) ;
            glTexCoordPointer( 2 , GL_FLOAT , vertexFormatSize , offsetTu ) ;
            glVertexPointer  ( 3 , GL_FLOAT , vertexFormatSize , offsetPx ) ;
        }
        #endif

        glEnableClientState( GL_VERTEX_ARRAY ) ;
        glEnableClientState( GL_TEXTURE_COORD_ARRAY ) ;
    }
    else
#endif
    {   // Use old-style vertex arrays (not vertex buffer objects)
        glInterleavedArrays( vertexFormatFlags , vertexFormatSize , mVertexBuffer ) ;
    }

    // Draw the primitives
    glDrawArrays( GL_QUADS , 0 , (GLsizei) numVertices ) ;
    //glDrawArrays( GL_POINTS , 0 , (GLsizei) numVertices ) ;

    QUERY_PERFORMANCE_EXIT( ParticlesRender_Draw ) ;

    QUERY_PERFORMANCE_EXIT( ParticlesRender ) ;
}
