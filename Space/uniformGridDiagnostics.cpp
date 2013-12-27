/*! \file uniformGridDiagnostics.cpp

    \brief Diagnostic routines for UniformGrids of vectors or matrices

    \see Accompanying articles for more information:
        http://software.intel.com/en-us/articles/fluid-simulation-for-video-games-part-1/

    \author Copyright 2009 Dr. Michael Jason Gourlay; All rights reserved.
*/

#include "Core/Math/mat33.h"

#include "uniformGrid.h"



/*! \brief Generate brick-of-bytes volumetric data file, for a scalar quantity

    \param strFilenameBase - prefix of name of files to write.
        This string precedes both data and script file names.

    \param uFrame - frame number.  Used to generate data filenames.

    This routine also appends filenames to an OGLE script file named "<strFilenameBase>.ogle".

    Example OGLE script file preamble which can make use of the files this routine generates:

    \verbatim

windowSize: 512 512
Transform: { angles 270 0 0 } 
#
dataView: Opacity_Renderer[0] subset.selected_var = 0
dataView: Opacity_Renderer[0] colormap.alpha      = 0.5
dataView: Opacity_Renderer[0] colormap[0].type    = COLORMAP_SPECTRUM
dataView: Opacity_Renderer[0] colormap[1].alpha   = 0.6
dataView: Opacity_Renderer[0] colormap[2].alpha   = 0.7
dataView: Opacity_Renderer[0] active              = true
#
# Entries for various data sets:
#
# (Append contents of file generated by this routine.)

    \endverbatim

*/
void UniformGrid<float>::GenerateBrickOfBytes( const char * strFilenameBase , unsigned uFrame ) const
{
#if ! defined( _XBOX )
    // Compute min, max values of values.
    float fMin=FLT_MAX, fMax=-fMin ;
    ComputeStatistics( fMin , fMax ) ;
#if ENFORCE_SYMMETRIC_RANGE
    const float fExtreme = MAX2( -fMin , fMax ) ;
    fMax = fExtreme ;
    fMin = - fMax ;
#endif
    const float        fRange = MAX2( fMax - fMin , FLT_MIN ) ; // FLT_MIN is to avoid divide-by-zero below.

    // Create name of data file.
    char strDataFilename[ 256 ] ;
    sprintf( strDataFilename , "Vols/%s%05u-%ux%ux%u.dat" , strFilenameBase , uFrame , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) ) ;
    {
        // Append data filename to script file.
        char strScriptFilename[ 256 ] ;
        sprintf( strScriptFilename , "%s.ogle" , strFilenameBase ) ;
        FILE * pScriptFile = fopen( strScriptFilename , "a" ) ;
        if( ! pScriptFile ) { ASSERT( 0 ) ; return ; }
        // Write comment to script file indicating vector value ranges
        fprintf( pScriptFile , "# %s ranges: %9.7g to %9.7g\n" , strFilenameBase , fMin , fMax ) ;
        // Write to script file, names of vector component data files.
        fprintf( pScriptFile , "%ux%ux%u %s\n" , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) , strDataFilename ) ;
        fclose( pScriptFile ) ;
    }

    // Open and populate data file.
    FILE * pDataFile ;
    pDataFile = fopen( strDataFilename , "wb" ) ;
    if( ! pDataFile ) { ASSERT( 0 ) ; return ; }

    static const float fAlmost256 = 256.0f * ( 1.0f - FLT_EPSILON ) ;

    const unsigned numGridPoints = Size() ;
    for( unsigned offset = 0 ; offset < numGridPoints ; ++ offset )
    {
        const float  &  rVal        = (*this)[ offset ] ;
        const float     fShifted    = rVal - fMin ;
        ASSERT( fShifted >= 0.0f ) ;
        const float     f0to1       = fShifted / fRange ;
        ASSERT( ( f0to1 >= 0.0f ) && ( f0to1 <= 1.0f ) ) ;
        const float     f0to255     = f0to1 * fAlmost256 ;
        const int       iVal        =  f0to255 ;
        ASSERT( ( iVal >= 0 ) && ( iVal <= 255 ) ) ;
        unsigned char   cVal        = iVal ;
        fwrite( & cVal , 1 , 1 , pDataFile ) ;
    }

    // Write minimum and maximum values.
    // Without this, OGLE will interpret the value to be unsigned.
    fprintf( pDataFile , "MIN %g MAX %g\n" , fMin , fMax  ) ;

    // Close data file.
    fclose( pDataFile ) ;
#endif
}




/* static */ void UniformGrid<unsigned>::UnitTest( void )
{
#if defined( _DEBUG )
    static unsigned const num = 1024 ;
    static const Vec3 vRange( 2.0f , 3.0f , 5.0f ) ; // range of random positions

    {
        // Create a set of points at random locations.
        Vec3 vPositions[ num ] ;
        Vec3 vMin(  1.0e8f ,  1.0e8f ,  1.0e8f ) ;
        Vec3 vMax( -1.0e8f , -1.0e8f , -1.0e8f ) ;
        for( unsigned idx = 0 ; idx < num ; ++ idx )
        {   // For each particle slot...
            // Populate array with random positions
            vPositions[ idx ].x = vRange.x * ( float( rand() ) / float( RAND_MAX ) - 0.5f ) ;
            vPositions[ idx ].y = vRange.y * ( float( rand() ) / float( RAND_MAX ) - 0.5f ) ;
            vPositions[ idx ].z = vRange.z * ( float( rand() ) / float( RAND_MAX ) - 0.5f ) ;
            // Calculate actual range of positions.
            vMin.x = MIN2( vMin.x , vPositions[ idx ].x ) ;
            vMin.y = MIN2( vMin.y , vPositions[ idx ].y ) ;
            vMin.z = MIN2( vMin.z , vPositions[ idx ].z ) ;
            vMax.x = MAX2( vMax.x , vPositions[ idx ].x ) ;
            vMax.y = MAX2( vMax.y , vPositions[ idx ].y ) ;
            vMax.z = MAX2( vMax.z , vPositions[ idx ].z ) ;
        }

        // Create a UniformGrid that exactly contains those points (i.e. its bounding box exactly fits the region containing all the points).
        UniformGrid<unsigned> ugm( num , vMin , vMax , true ) ;
        ugm.Init() ;
        const unsigned capacity = ugm.GetNumPoints( 0 ) * ugm.GetNumPoints( 1 ) * ugm.GetNumPoints( 2 ) ;
        fprintf( stderr , "dims=%5u , %5u , %5u , cap=%u spacing={ %g , %g , %g }\n" ,
            ugm.GetNumPoints( 0 ) , ugm.GetNumPoints( 1 ) , ugm.GetNumPoints( 2 ) ,
            capacity ,
            ugm.GetCellSpacing().x , ugm.GetCellSpacing().y , ugm.GetCellSpacing().z ) ;

        // Insert each point into the UniformGrid.
        for( unsigned idx = 0 ; idx < num ; ++ idx )
        {   // For each point...
            // Populate grid with random positions.
            ugm[ vPositions[ idx ] ] = idx ;
        }

        // Iterate through each cell in the UniformGrid and obtains its contents.
        {
            unsigned idx[3] ;
            const numXY = ugm.GetNumPoints( 0 ) * ugm.GetNumPoints( 1 ) ;
            for( idx[2] = 0 ; idx[2] < ugm.GetNumPoints( 2 ) ; ++ idx[2] )
            {
                const Vec3 Nudge = 2.0f * FLT_EPSILON * ugm.GetExtent() ; // Shift each query point slightly to inside each cell.
                const unsigned offsetZ = idx[2] * numXY ;
                Vec3 vPosMin ;  // Position inside cell, near minimal corner
                vPosMin.z = vMin.z + float( idx[2] ) * ugm.GetCellSpacing().z + Nudge.z ;
                Vec3 vPosMax ; // Position inside cell, near maximal corner
                vPosMax.z = vPosMin.z + ugm.GetCellSpacing().z - 2.0f * Nudge.z ;
                for( idx[1] = 0 ; idx[1] < ugm.GetNumPoints( 1 ) ; ++ idx[1] )
                {
                    const unsigned offsetYZ = idx[1] * ugm.GetNumPoints( 0 ) + offsetZ ;
                    vPosMin.y = vMin.y + float( idx[1] ) * ugm.GetCellSpacing().y + Nudge.y ;
                    vPosMax.y = vPosMin.y + ugm.GetCellSpacing().y - 2.0f * Nudge.y ;
                    for( idx[0] = 0 ; idx[0] < ugm.GetNumPoints( 0 ) ; ++ idx[0] )
                    {
                        const unsigned      offsetXYZ           = idx[0] + offsetYZ ;
                        const unsigned      offsetFromIndices   = ugm.OffsetFromIndices( idx ) ;
                        ASSERT( offsetXYZ == offsetFromIndices ) ; // Compare 2 ways of computing offset from indices
                        unsigned &          contentsFromOffset  = ugm[ offsetXYZ ] ;

                        vPosMin.x = vMin.x + float( idx[0] ) * ugm.GetCellSpacing().x + Nudge.x ;
                        vPosMax.x = vPosMin.x + ugm.GetCellSpacing().x - 2.0f * Nudge.x ;

                        {   // Make sure vPosMin is inside the cell we expect it to be in.
                            unsigned            indicesFromPos[3] ;
                            ugm.IndicesOfPosition( indicesFromPos , vPosMin ) ;
                            ASSERT( ( indicesFromPos[0] == idx[0] ) && ( indicesFromPos[1] == idx[1] ) && ( indicesFromPos[2] == idx[2] ) ) ;
                            const unsigned      offsetOfPos         = ugm.OffsetOfPosition( vPosMin ) ;
                            ASSERT( offsetOfPos == offsetXYZ ) ;    // Compare 2 ways of computing offset from position
                            unsigned &          contentsFromPos     = ugm[ vPosMin ] ;
                            ASSERT( & contentsFromPos == & contentsFromOffset ) ; // Compare 2 ways of accessing contents.
                        }
                        {   // Make sure vPosMax is inside the cell we expect it to be in.
                            unsigned            indicesFromPos[3] ;
                            ugm.IndicesOfPosition( indicesFromPos , vPosMax ) ;
                            ASSERT( ( indicesFromPos[0] == idx[0] ) && ( indicesFromPos[1] == idx[1] ) && ( indicesFromPos[2] == idx[2] ) ) ;
                            const unsigned      offsetOfPos         = ugm.OffsetOfPosition( vPosMax ) ;
                            ASSERT( offsetOfPos == offsetXYZ ) ;    // Compare 2 ways of computing offset from position
                            unsigned &          contentsFromPos     = ugm[ vPosMin ] ;
                            ASSERT( & contentsFromPos == & contentsFromOffset ) ; // Compare 2 ways of accessing contents.
                        }

                        // fprintf( stderr , "In cell at {%g %g %g} &=%p val=%u\n" , vPosMin.x , vPosMin.y , vPosMin.z , & contents , contents ) ;
                    }
                }
            }
        }
    }

    // Create a UniformGrid with a given domain and manually insert points at the /center/ of each cell.
    // Note that (in contrast to the test above, which is more typical of a real-world situation)
    // this test below would NOT include points exactly at vMin and vMax since those lie at boundaries of cells, not in the cell interior.
    {
        Vec3 vMin( -0.5f * vRange ) ;   // Minimum coordinate of UniformGrid.
        Vec3 vMax(  0.5f * vRange ) ;   // Maximum coordinate of UniformGrid.
        UniformGrid<unsigned> ugm2( num , vMin , vMax , true ) ;
        ugm2.Init() ;
        unsigned        counter     = 0 ;  // Tally of items added to UniformGrid.
        unsigned        idx[3] ;   // Grid cell indices.
        {
            const Vec3 vSpacing( vRange.x / float( ugm2.GetNumPoints( 0 ) - 1 ) , vRange.y / float( ugm2.GetNumPoints( 1 ) - 1 ) , vRange.z / float( ugm2.GetNumPoints( 2 ) - 1 ) ) ;
            ASSERT( ugm2.GetCellSpacing().Resembles( vSpacing ) ) ;
        }
        const Vec3 vOffset( vMin + 0.5f * ugm2.GetCellSpacing() ) ;
        for( idx[2] = 0 ; idx[2] < ugm2.GetNumPoints( 2 ) ; ++ idx[2] )
        {   // For each point along z axis...
            Vec3 vPos ; // Position of point within UniformGrid.
            vPos.z = vOffset.z + float( idx[2] ) * ugm2.GetCellSpacing().z ; // Compute position of /center/ of this grid cell.
            for( idx[1] = 0 ; idx[1] < ugm2.GetNumPoints( 1 ) ; ++ idx[1] )
            {   // For each point along y axis...
                vPos.y = vOffset.y + float( idx[1] ) * ugm2.GetCellSpacing().y ; // Compute position of /center/ of this grid cell.
                for( idx[0] = 0 ; idx[0] < ugm2.GetNumPoints( 0 ) ; ++ idx[0] )
                {   // For each point along x axis...
                    vPos.x = vOffset.x + float( idx[0] ) * ugm2.GetCellSpacing().x ; // Compute position of /center/ of this grid cell.
                    ugm2[ vPos ] = counter ;                                // Insert a single value at the center of this cell.
                    unsigned indices[3] ;                                   // Alternative way to compute same values of idx[].
                    ugm2.IndicesFromOffset( indices , counter ) ;           // Get component indices from composite.
                    ASSERT( indices[0] == idx[0] ) ;                        // Make sure IndicesFromIndex works.
                    ASSERT( indices[1] == idx[1] ) ;                        // Make sure IndicesFromIndex works.
                    ASSERT( indices[2] == idx[2] ) ;                        // Make sure IndicesFromIndex works.
                    Vec3 vPosCheck ;                                        // Other way to compute grid cell center.
                    ugm2.PositionFromOffset( vPosCheck , counter ) ;        // Get position of grid cell minimum.
                    ASSERT( vPos.Resembles( vPosCheck + 0.5f * ugm2.GetCellSpacing() ) ) ; // Compute grid cell center using 2 different techniques.
                    ++ counter ;                                            // Tally the total number of items added to this container.
                }
            }
        }
        const unsigned capacity = ugm2.GetGridCapacity() ;
        ASSERT( counter == capacity ) ;
        for( unsigned i = 0 ; i < capacity ; ++ i )
        {   // Iterate through each cell.
            unsigned & contents = ugm2.mContents[ i ] ;
            ASSERT( contents == i ) ;
            //fprintf( stderr , "element [%u]=%u\n" , i , contents ) ;
        }
    }

    {
        // Create a lower level-of-detail container based on another.
        Vec3 vMin( -0.5f * vRange ) ;   // Minimum coordinate of UniformGrid.
        Vec3 vMax(  0.5f * vRange ) ;   // Maximum coordinate of UniformGrid.
        UniformGrid<unsigned> ugm3a( num , vMin , vMax , true ) ;
        UniformGrid<unsigned> ugm3b ;
        static const unsigned decimation = 2 ;
        ugm3b.Decimate( ugm3a , decimation ) ;
        ASSERT( ugm3b.GetNumCells( 0 ) == ugm3a.GetNumCells( 0 ) / decimation ) ;
        ASSERT( ugm3b.GetNumCells( 1 ) == ugm3a.GetNumCells( 1 ) / decimation ) ;
        ASSERT( ugm3b.GetNumCells( 2 ) == ugm3a.GetNumCells( 2 ) / decimation ) ;
    }
#endif
}




/*! \brief Compute statistics of data in a uniform grid of 3-vectors

    \param min - minimum of all values in grid.

    \param max - maximum of all values in grid.

*/
void UniformGrid<Vec3>::ComputeStatistics( Vec3 & min , Vec3 & max ) const
{
    min = Vec3( FLT_MAX , FLT_MAX , FLT_MAX ) ;
    max = Vec3( -min ) ;
    const unsigned dims[3] = { GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) } ;
    const unsigned numXY   = dims[0] * dims[1] ;
    unsigned index[3] ;

    for( index[2] = 0 ; index[2] < dims[2] ; ++ index[2] )
    {
        const unsigned offsetPartialZ = numXY * index[2]       ;
        for( index[1] = 0 ; index[1] < dims[1] ; ++ index[1] )
        {
            const unsigned offsetPartialYZ = dims[ 0 ] * index[1] + offsetPartialZ ;
            for( index[0] = 0 ; index[0] < dims[0] ; ++ index[0] )
            {
                const unsigned offset = index[0]     + offsetPartialYZ  ;
                ASSERT( offset == index[0] + dims[ 0 ] * ( index[1] + dims[ 1 ] * index[2] ) ) ;
                const Vec3 & rVal = (*this)[ offset ] ;
                min.x = MIN2( min.x , rVal.x ) ;
                min.y = MIN2( min.y , rVal.y ) ;
                min.z = MIN2( min.z , rVal.z ) ;
                max.x = MAX2( max.x , rVal.x ) ;
                max.y = MAX2( max.y , rVal.y ) ;
                max.z = MAX2( max.z , rVal.z ) ;
            }
        }
    }
}




/*! \brief Generate brick-of-bytes volumetric data files, one per component of vector and another for magnitude.

    \param strFilenameBase - prefix of name of file to write

    \param uFrame - frame number.  Used to generate filenames.

    This routine also appends filenames to an OGLE script file.

    Example OGLE script file which can make use of the files this routine generates:

    \verbatim

windowSize: 512 512
Transform: { angles 270 0 0 } 
#
dataView: Streamline[0] subset.x_range = 31 32 4
dataView: Streamline[0] subset.y_range = 54 57 4
dataView: Streamline[0] subset.z_range =  7 10 4
dataView: Streamline[0] colormap.type  = COLORMAP_SPECTRUM
dataView: Streamline[0] criterion      = STREAMLINE_CRITERION_ALL
dataView: Streamline[0] decimation     = 5
dataView: Streamline[0] active         = true
#
dataView: Vector_Field subset.selected_var = 0
dataView: Vector_Field subset.x_stride = 2
dataView: Vector_Field subset.y_stride = 2
dataView: Vector_Field subset.z_stride = 2
dataView: Vector_Field colormap.type   = COLORMAP_HOT_COLD
dataView: Vector_Field vector_glyph    = HEDGEHOG_VECTOR_CONES_SOLID_LIT
dataView: Vector_Field active          = true
#
dataView: Opacity_Renderer[0] subset.selected_var = -1
dataView: Opacity_Renderer[0] colormap.alpha      = 0.5
dataView: Opacity_Renderer[0] colormap[0].type    = COLORMAP_SPECTRUM
dataView: Opacity_Renderer[0] colormap[1].alpha   = 0.6
dataView: Opacity_Renderer[0] colormap[2].alpha   = 0.7
dataView: Opacity_Renderer[0] active              = true
#
# Entries for various data sets:
#
# (Append contents of file generated by this routine.)

    \endverbatim

*/
void UniformGrid<Vec3>::GenerateBrickOfBytes( const char * strFilenameBase , unsigned uFrame ) const
{
#if ! defined( _XBOX )
    // Compute min, max values of vector components.
    Vec3 vMin , vMax ;
    ComputeStatistics( vMin , vMax ) ;
    Vec3        vExtreme( MAX2( -vMin.x , vMax.x ) , MAX2( -vMin.y , vMax.y ) , MAX2( -vMin.z , vMax.z ) ) ;
    const float fMagMax = vExtreme.Magnitude() ; // Not the correct value for |v|_max but a reasonable approximation for visualization purposes.
#if ENFORCE_SYMMETRIC_RANGE
    vMax = vExtreme ;
    vMin = - vMax ;
#endif
    Vec3        vRange( vMax - vMin ) ;
    vRange.x = MAX2( FLT_MIN , vRange.x ) ;  // Avoid divide-by-zero below
    vRange.y = MAX2( FLT_MIN , vRange.y ) ;  // Avoid divide-by-zero below
    vRange.z = MAX2( FLT_MIN , vRange.z ) ;  // Avoid divide-by-zero below

    // Create names of data files.
    char strDataFilenames[4][ 256 ] ;
    sprintf( strDataFilenames[0] , "Vols/%sX%05u-%ux%ux%u.dat" , strFilenameBase , uFrame , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) ) ;
    sprintf( strDataFilenames[1] , "Vols/%sY%05u-%ux%ux%u.dat" , strFilenameBase , uFrame , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) ) ;
    sprintf( strDataFilenames[2] , "Vols/%sZ%05u-%ux%ux%u.dat" , strFilenameBase , uFrame , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) ) ;
    sprintf( strDataFilenames[3] , "Vols/%sM%05u-%ux%ux%u.dat" , strFilenameBase , uFrame , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) ) ;
    {
        // Append data filenames to script file.
        char strScriptFilename[ 256 ] ;
        sprintf( strScriptFilename , "%s.ogle" , strFilenameBase ) ;
        FILE * pScriptFile = fopen( strScriptFilename , "a" ) ;
        // Write comment to script file indicating vector value ranges
        fprintf( pScriptFile , "# %s ranges: {%9.7g,%9.7g,%9.7g} to {%9.7g,%9.7g,%9.7g}\n" , strFilenameBase , vMin.x , vMin.y , vMin.z , vMax.x , vMax.y , vMax.z ) ;
        // Write to script file, names of vector component data files.
        fprintf( pScriptFile , "%ux%ux%u %s %s %s\n" , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) , strDataFilenames[0] , strDataFilenames[1] , strDataFilenames[2] ) ;
        // Write to script file, name of magnitude data file.
        // fprintf( pScriptFile , "%ux%ux%u %s\n" , GetNumPoints( 0 ) , GetNumPoints( 1 ) , GetNumPoints( 2 ) , strDataFilenames[3] ) ;
        fclose( pScriptFile ) ;
    }

    // Open and populate data files.
    FILE * pDataFiles[4] ;
    pDataFiles[0] = fopen( strDataFilenames[0] , "wb" ) ;
    pDataFiles[1] = fopen( strDataFilenames[1] , "wb" ) ;
    pDataFiles[2] = fopen( strDataFilenames[2] , "wb" ) ;
    pDataFiles[3] = fopen( strDataFilenames[3] , "wb" ) ;
    if( ! pDataFiles[0] ) { ASSERT( 0 ) ; return ; }
    if( ! pDataFiles[1] ) { ASSERT( 0 ) ; return ; }
    if( ! pDataFiles[2] ) { ASSERT( 0 ) ; return ; }
    if( ! pDataFiles[3] ) { ASSERT( 0 ) ; return ; }

    static const float fAlmost256 = 256.0f * ( 1.0f - FLT_EPSILON ) ;

    const unsigned numGridPoints = Size() ;
    for( unsigned offset = 0 ; offset < numGridPoints ; ++ offset )
    {
        const Vec3   &  rVec            = (*this)[ offset ] ;
        const float     fMag            = rVec.Magnitude() ;
        ASSERT( ( fMag >= 0.0f ) && ( fMag <= fMagMax ) ) ;
        const Vec3      vShifted        = rVec - vMin ;
        ASSERT( vShifted.x >= 0.0f ) ;
        ASSERT( vShifted.y >= 0.0f ) ;
        ASSERT( vShifted.z >= 0.0f ) ;
        const Vec3      v0to1        = Vec3( vShifted.x / vRange.x , vShifted.y / vRange.y , vShifted.z / vRange.z ) ;
        ASSERT( ( v0to1.x >= 0.0f ) && ( v0to1.x <= 1.0f ) ) ;
        ASSERT( ( v0to1.y >= 0.0f ) && ( v0to1.y <= 1.0f ) ) ;
        ASSERT( ( v0to1.z >= 0.0f ) && ( v0to1.z <= 1.0f ) ) ;
        const Vec3      v0to255      = v0to1 * fAlmost256 ;
        ASSERT( ( v0to255.x >= 0.0f ) && ( v0to255.x < 256.0f ) ) ;
        ASSERT( ( v0to255.y >= 0.0f ) && ( v0to255.y < 256.0f ) ) ;
        ASSERT( ( v0to255.z >= 0.0f ) && ( v0to255.z < 256.0f ) ) ;
        const float     vMag0to255   = fAlmost256 * fMag / fMagMax ;
        //printf( "vF %3g %3g %3g %3g %3g\n", v0to255.x , v0to255.y , v0to255.z , vMag0to255 ) ;
        int iVec[4] = { v0to255.x , v0to255.y , v0to255.z , vMag0to255 } ;
        ASSERT( ( iVec[0] >= 0 ) && ( iVec[0] <= 255 ) ) ;
        ASSERT( ( iVec[1] >= 0 ) && ( iVec[1] <= 255 ) ) ;
        ASSERT( ( iVec[2] >= 0 ) && ( iVec[2] <= 255 ) ) ;
        ASSERT( ( iVec[3] >= 0 ) && ( iVec[3] <= 255 ) ) ;
    #if ENFORCE_SYMMETRIC_RANGE
        ASSERT( ( ( rVec.x >= 0.f ) || ( iVec[0] <= 127 ) ) && ( ( rVec.x <= 0.f ) || ( iVec[0] >= 127 ) ) ) ;
        ASSERT( ( ( rVec.y >= 0.f ) || ( iVec[1] <= 127 ) ) && ( ( rVec.y <= 0.f ) || ( iVec[1] >= 127 ) ) ) ;
        ASSERT( ( ( rVec.z >= 0.f ) || ( iVec[2] <= 127 ) ) && ( ( rVec.z <= 0.f ) || ( iVec[2] >= 127 ) ) ) ;
    #endif
        //printf( "vI %3i %3i %3i %3i\n", iVec[0] , iVec[1] , iVec[2] , iVec[3] ) ;
        unsigned char cVec[4] = { iVec[0] , iVec[1] , iVec[2] , iVec[3] } ;
        fwrite( & cVec[0] , 1 , 1 , pDataFiles[0] ) ;
        fwrite( & cVec[1] , 1 , 1 , pDataFiles[1] ) ;
        fwrite( & cVec[2] , 1 , 1 , pDataFiles[2] ) ;
        fwrite( & cVec[3] , 1 , 1 , pDataFiles[3] ) ;
        //printf( "vC %3u %3u %3u %3u\n", cVec[0] , cVec[1] , cVec[2] , cVec[3] ) ;
    }

    // Write minimum and maximum values for each component.
    // Without this, OGLE will interpret the value to be signed, when used with hedgehog and streamline dataviews.
    fprintf( pDataFiles[0] , "MIN %g MAX %g\n" , vMin.x , vMax.x  ) ;
    fprintf( pDataFiles[1] , "MIN %g MAX %g\n" , vMin.y , vMax.y  ) ;
    fprintf( pDataFiles[2] , "MIN %g MAX %g\n" , vMin.z , vMax.z  ) ;
    fprintf( pDataFiles[3] , "MIN %g MAX %g\n" , 0      , fMagMax ) ;

    // Close data files.
    fclose( pDataFiles[0] ) ;
    fclose( pDataFiles[1] ) ;
    fclose( pDataFiles[2] ) ;
    fclose( pDataFiles[3] ) ;
#endif
}
