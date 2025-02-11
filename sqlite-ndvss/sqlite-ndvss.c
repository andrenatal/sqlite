/*
** This SQLite extension implements functions to perform vector
** similarity searches.
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <stdlib.h>
#include <math.h>
//#define USE_AVX 1 // Comment this out if you don't want to use AVX extensions.
#ifdef USE_AVX
#include <immintrin.h>
#endif


#define NDVSS_VERSION_DOUBLE  0.45


//----------------------------------------------------------------------------------------
// Name: ndvss_convert_str_to_array_d
// Desc: Returns the current version of ndvss.
// Args: None.
// Returns: Version number as a DOUBLE.
//----------------------------------------------------------------------------------------
static void ndvss_version( sqlite3_context* context,
                           int argc,
                           sqlite3_value** argv )
{
  sqlite3_result_double(context, NDVSS_VERSION_DOUBLE );
}


//----------------------------------------------------------------------------------------
// Name: ndvss_convert_str_to_array_d
// Desc: Converts a list of decimal numbers from a string to an array of doubles.
// Args: List of decimal numbers TEXT,
//       Number of dimensions INTEGER
// Returns: The double-array as a BLOB.
//----------------------------------------------------------------------------------------
static void ndvss_convert_str_to_array_d( sqlite3_context* context,
                                          int argc,
                                          sqlite3_value** argv )
{
  if( argc < 2 ) {
    sqlite3_result_error(context, "2 arguments needs to be given: string to convert, array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }

  int num_dimensions = sqlite3_value_int(argv[1]);
  if( num_dimensions <= 0 ) {
    sqlite3_result_error(context, "Number of dimensions is 0.", -1);
    return;
  }
  int allocated_size = sizeof(double)*num_dimensions;
  double* output = (double*)sqlite3_malloc(allocated_size);
  if( output == 0 ) {
    sqlite3_result_error(context, "Out of memory.", -1);
    return;
  }
  char* input = (char*)sqlite3_value_text(argv[0]);
  char* end = input;
  double* index = output;
  int i = 0;
  while( end != 0 && i < num_dimensions ) {
    // Skip the JSON-array characters.
    if (*end == '[' || *end == ']' || *end == ',') {
      end++;
      continue;
    }
    *index = strtod(end, &end);
    ++index;
    ++i;
  }//endwhile processing string
  sqlite3_result_blob(context, output, allocated_size, sqlite3_free );
}


//----------------------------------------------------------------------------------------
// Name: ndvss_convert_str_to_array_f
// Desc: Converts a list of decimal numbers from a string to an array of floats.
// Args: List of decimal numbers TEXT,
//       Number of dimensions INTEGER
// Returns: The float-array as a BLOB.
//----------------------------------------------------------------------------------------
static void ndvss_convert_str_to_array_f( sqlite3_context* context,
                                          int argc,
                                          sqlite3_value** argv )
{
  if( argc < 2 ) {
    sqlite3_result_error(context, "2 arguments needs to be given: string to convert, array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }

  int num_dimensions = sqlite3_value_int(argv[1]);
  if( num_dimensions <= 0 ) {
    sqlite3_result_error(context, "Number of dimensions is 0.", -1);
    return;
  }
  int allocated_size = sizeof(float)*num_dimensions;
  float* output = (float*)sqlite3_malloc(allocated_size);
  if( output == 0 ) {
    sqlite3_result_error(context, "Out of memory.", -1);
    return;
  }
  char* input = (char*)sqlite3_value_text(argv[0]);
  char* end = input;
  float* index = output;
  int i = 0;
  while( end != 0 && i < num_dimensions ) {
    // Skip the JSON-array characters.
    if (*end == '[' || *end == ']' || *end == ',') {
      end++;
      continue;
    }
    *index = (float)strtod(end, &end);
    ++index;
    ++i;
  }//endwhile processing string
  sqlite3_result_blob(context, output, allocated_size, sqlite3_free );
}


//----------------------------------------------------------------------------------------
// Name: ndvss_cosine_similarity_d
// Desc: Calculates the cosine similarity to a BLOB-converted array of doubles.
// Args: Searched double array BLOB,
//       Compared double array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as an angle DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_cosine_similarity_d( sqlite3_context* context,
                                       int argc,
                                       sqlite3_value** argv )
{
  if( argc < 2 ) {
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array. Optionally the vector size can be given as the 3rd argument.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(double);
  }

  const double* searched_array = (const double *)sqlite3_value_blob(argv[0]);
  const double* column_array = (const double *)sqlite3_value_blob(argv[1]);
  double similarity = 0.0;
  double dividerA = 0.0;
  double dividerB = 0.0;
  int i = 0;
  #ifdef USE_AVX
  __m256d A, B, AA, BB, AB, mmdividerA = _mm256_setzero_pd(), mmdividerB = _mm256_setzero_pd(), mmsimilarity = _mm256_setzero_pd();
  for( ; i + 3 < vector_size; i += 4 ) {
    A = _mm256_loadu_pd(&searched_array[i]);
    B = _mm256_loadu_pd(&column_array[i]);
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    mmdividerA = _mm256_fmadd_pd(A, A, mmdividerA);
    mmdividerB = _mm256_fmadd_pd(B, B, mmdividerB);
    mmsimilarity = _mm256_fmadd_pd(A, B, mmsimilarity);
    #else
    // No Fused multiply-add support (AVX).
    AA = _mm256_mul_pd(A, A);
    BB = _mm256_mul_pd(B, B);
    AB = _mm256_mul_pd(A, B);
    mmdividerA = _mm256_add_pd(AA, mmdividerA);
    mmdividerB = _mm256_add_pd(BB, mmdividerB);
    mmsimilarity = _mm256_add_pd(AB, mmsimilarity);
    #endif
  }
  // The following is based on code from stack overflow.
  // https://stackoverflow.com/questions/49941645/get-sum-of-values-stored-in-m256d-with-sse-avx/49943540#49943540
  __m128d vlow  = _mm256_castpd256_pd128(mmdividerA);
  __m128d vhigh = _mm256_extractf128_pd(mmdividerA, 1);
          vlow  = _mm_add_pd(vlow, vhigh);

  __m128d high64 = _mm_unpackhi_pd(vlow, vlow);
  dividerA = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));

  vlow  = _mm256_castpd256_pd128(mmdividerB);
  vhigh = _mm256_extractf128_pd(mmdividerB, 1);
  vlow  = _mm_add_pd(vlow, vhigh);

  high64 = _mm_unpackhi_pd(vlow, vlow);
  dividerB = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));

  vlow  = _mm256_castpd256_pd128(mmsimilarity);
  vhigh = _mm256_extractf128_pd(mmsimilarity, 1);
  vlow  = _mm_add_pd(vlow, vhigh);

  high64 = _mm_unpackhi_pd(vlow, vlow);
  similarity = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));
  #else
  // Non-AVX implementation.
  for( ; i + 3 < vector_size; i += 4 ) {
    double A = searched_array[i];
    double B = column_array[i];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+1];
    B = column_array[i+1];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);
    A = searched_array[i+2];
    B = column_array[i+2];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);
     A = searched_array[i+3];
    B = column_array[i+3];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);
  }
  #endif

  for(; i < vector_size; ++i ) {
    double Ax = searched_array[i];
    double Bx = column_array[i];
    similarity += (Ax*Bx);
    dividerA += (Ax*Ax);
    dividerB += (Bx*Bx);
  }

  if( dividerA == 0.0 || dividerB == 0.0 ) {
    sqlite3_result_error(context, "Division by zero.", -1);
    return;
  }
  double divider = sqrt(dividerA * dividerB);
  similarity = similarity / divider;
  sqlite3_result_double(context, similarity);

}


//----------------------------------------------------------------------------------------
// Name: ndvss_cosine_similarity_f
// Desc: Calculates the cosine similarity to a BLOB-converted array of floats.
// Args: Searched float array BLOB,
//       Compared float array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as an angle DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_cosine_similarity_f( sqlite3_context* context,
                                       int argc,
                                       sqlite3_value** argv )
{
  if( argc < 2 ) {
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally the array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    sqlite3_result_error(context, "One of the required arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(float);
  }

  const float* searched_array = (const float *)sqlite3_value_blob(argv[0]);
  const float* column_array = (const float *)sqlite3_value_blob(argv[1]);
  float similarity = 0.0f;
  float dividerA = 0.0f;
  float dividerB = 0.0f;
  int i = 0;
  #ifdef USE_AVX
  __m256 A, B, AA, BB, AB, mmdividerA = _mm256_setzero_ps(), mmdividerB = _mm256_setzero_ps(), mmsimilarity = _mm256_setzero_ps();
  for( ; i + 7 < vector_size; i += 8 ) {
    A = _mm256_loadu_ps(&searched_array[i]);
    B = _mm256_loadu_ps(&column_array[i]);
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    mmdividerA = _mm256_fmadd_ps(A, A, mmdividerA);
    mmdividerB = _mm256_fmadd_ps(B, B, mmdividerB);
    mmsimilarity = _mm256_fmadd_ps(A, B, mmsimilarity);
    #else
    // No Fused multiply-add support (AVX).
    AA = _mm256_mul_ps(A, A);
    BB = _mm256_mul_ps(B, B);
    AB = _mm256_mul_ps(A, B);
    mmdividerA = _mm256_add_ps(AA, mmdividerA);
    mmdividerB = _mm256_add_ps(BB, mmdividerB);
    mmsimilarity = _mm256_add_ps(AB, mmsimilarity);
    #endif
  }

  __m128 vlow   = _mm256_castps256_ps128(mmdividerA);
  __m128 vhigh  = _mm256_extractf128_ps(mmdividerA, 1);
         vlow   = _mm_add_ps(vlow, vhigh);
  __m128 high64 = _mm_movehl_ps( vlow, vlow );
  __m128 sum    = _mm_add_ps(vlow, high64);
         sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  dividerA = _mm_cvtss_f32(sum);

  vlow   = _mm256_castps256_ps128(mmdividerB);
  vhigh  = _mm256_extractf128_ps(mmdividerB, 1);
  vlow   = _mm_add_ps(vlow, vhigh);
  high64 = _mm_movehl_ps( vlow, vlow );
  sum    = _mm_add_ps(vlow, high64);
  sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  dividerB = _mm_cvtss_f32(sum);

  vlow   = _mm256_castps256_ps128(mmsimilarity);
  vhigh  = _mm256_extractf128_ps(mmsimilarity, 1);
  vlow   = _mm_add_ps(vlow, vhigh);
  high64 = _mm_movehl_ps( vlow, vlow );
  sum    = _mm_add_ps(vlow, high64);
  sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  similarity = _mm_cvtss_f32(sum);



  #else
  //#pragma GCC ivdep
  /**/ //
  //for( ; i + 7 < vector_size; i += 8 ) {
  for( ; i + 3 < vector_size; i += 4 ) {
    float A = searched_array[i];
    float B = column_array[i];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+1];
    B = column_array[i+1];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+2];
    B = column_array[i+2];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+3];
    B = column_array[i+3];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);
/**
    A = searched_array[i+4];
    B = column_array[i+4];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+5];
    B = column_array[i+5];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+6];
    B = column_array[i+6];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

    A = searched_array[i+7];
    B = column_array[i+7];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);
    **/
  }
  //#pragma GCC ivdep
  #endif
  /**/
  for(; i < vector_size; ++i ) {
    float A = searched_array[i];
    float B = column_array[i];
    similarity += (A*B);
    dividerA += (A*A);
    dividerB += (B*B);

  }
  if( dividerA == 0.0f || dividerB == 0.0f ) {
    // There'd be a division by zero, so assume no similarity.
    sqlite3_result_error(context, "Division by zero.", -1);
    return;
  }
  float divider = sqrtf(dividerA * dividerB);
  similarity = similarity / divider;
  sqlite3_result_double(context, (double)similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_euclidean_distance_similarity_d
// Desc: Calculates the euclidean distance similarity to a BLOB-converted array of doubles.
// Args: Searched double array BLOB,
//       Compared double array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a distance DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_euclidean_distance_similarity_d( sqlite3_context* context,
                                                   int argc,
                                                   sqlite3_value** argv )
{
  if( argc < 2 ) {
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally the array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    sqlite3_result_error(context, "One of the required arguments is null.", -1);
    return;
  }

  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(double);
  }

  const double* searched_array = (const double *)sqlite3_value_blob(argv[0]);
  const double* column_array = (const double *)sqlite3_value_blob(argv[1]);
  double similarity = 0.0;

  int i = 0;
  #ifdef USE_AVX
  __m256d A, B, AB, ABAB, sumAB = _mm256_setzero_pd();
  for( ; i + 3 < vector_size; i += 4 ) {
    A = _mm256_loadu_pd(&searched_array[i]);
    B = _mm256_loadu_pd(&column_array[i]);
    AB = _mm256_sub_pd( A, B );
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    sumAB = _mm256_fmadd_pd(AB, AB, sumAB );
    #else
    // No Fused multiply-add support (AVX).
    ABAB = _mm256_mul_pd(AB, AB);
    sumAB = _mm256_add_pd(ABAB, sumAB );
    #endif
  }
  __m128d vlow  = _mm256_castpd256_pd128(sumAB);
  __m128d vhigh = _mm256_extractf128_pd(sumAB, 1);
          vlow  = _mm_add_pd(vlow, vhigh);

  __m128d high64 = _mm_unpackhi_pd(vlow, vlow);
  similarity = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));

  #else

  for( ; i + 3 < vector_size; i += 4 ) {
    double AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
    AB = (searched_array[i+1] - column_array[i+1]);
    similarity += (AB * AB);
    AB = (searched_array[i+2] - column_array[i+2]);
    similarity += (AB * AB);
    AB = (searched_array[i+3] - column_array[i+3]);
    similarity += (AB * AB);
  }
  #endif
  for( ; i < vector_size; ++i ) {
    double AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
  }
  similarity = sqrt(similarity);
  sqlite3_result_double(context, similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_euclidean_distance_similarity_f
// Desc: Calculates the euclidean distance similarity to a BLOB-converted array of floats.
// Args: Searched float array BLOB,
//       Compared float array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a distance DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_euclidean_distance_similarity_f( sqlite3_context* context,
                                                   int argc,
                                                   sqlite3_value** argv )
{
  if( argc < 2 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally the array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    // Missing one of the required arguments.
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(float);
  }

  const float* searched_array = (const float *)sqlite3_value_blob(argv[0]);
  const float* column_array = (const float *)sqlite3_value_blob(argv[1]);
  float similarity = 0.0f;

  int i = 0;
  #ifdef USE_AVX
  __m256 A, B, AB, ABAB, sumAB = _mm256_setzero_ps();
  for( ; i + 7 < vector_size; i += 8 ) {
    A = _mm256_loadu_ps(&searched_array[i]);
    B = _mm256_loadu_ps(&column_array[i]);
    AB = _mm256_sub_ps( A, B );
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    sumAB = _mm256_fmadd_ps(AB, AB, sumAB );
    #else
    // No fused multiply-add support (AVX).
    ABAB = _mm256_mul_ps(AB, AB);
    sumAB = _mm256_add_ps(ABAB, sumAB);
    #endif
  }
  __m128 vlow   = _mm256_castps256_ps128(sumAB);
  __m128 vhigh  = _mm256_extractf128_ps(sumAB, 1);
         vlow   = _mm_add_ps(vlow, vhigh);
  __m128 high64 = _mm_movehl_ps( vlow, vlow );
  __m128 sum    = _mm_add_ps(vlow, high64);
         sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  similarity = _mm_cvtss_f32(sum);

  #else

  for( ; i + 3 < vector_size; i += 4 ) {
    float AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
    AB = (searched_array[i+1] - column_array[i+1]);
    similarity += (AB * AB);
    AB = (searched_array[i+2] - column_array[i+2]);
    similarity += (AB * AB);
    AB = (searched_array[i+3] - column_array[i+3]);
    similarity += (AB * AB);
  }
  #endif
  for( ; i < vector_size; ++i ) {
    float AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
  }
  similarity = sqrtf(similarity);
  sqlite3_result_double(context, (double)similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_euclidean_distance_similarity_squared_d
// Desc: Calculates the euclidean distance similarity to a BLOB-converted array of doubles.
//       Returns the squared result (i.e. doesn't calculate the square root).
// Args: Searched double array BLOB,
//       Compared double array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a squared distance DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_euclidean_distance_similarity_squared_d( sqlite3_context* context,
                                                           int argc,
                                                           sqlite3_value** argv )
{
  if( argc < 2 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    // Missing one of the required arguments.
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(double);
  }

  const double* searched_array = (const double *)sqlite3_value_blob(argv[0]);
  const double* column_array = (const double *)sqlite3_value_blob(argv[1]);
  double similarity = 0.0;

  //#pragma GCC ivdep
  int i = 0;
  #ifdef USE_AVX
  __m256d A, B, AB, ABAB, sumAB = _mm256_setzero_pd();
  for( ; i + 3 < vector_size; i += 4 ) {
    A = _mm256_loadu_pd(&searched_array[i]);
    B = _mm256_loadu_pd(&column_array[i]);
    AB = _mm256_sub_pd( A, B );
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    sumAB = _mm256_fmadd_pd(AB, AB, sumAB );
    #else
    // No fused multiply-add support (AVX).
    ABAB = _mm256_mul_pd(AB, AB);
    sumAB = _mm256_add_pd(ABAB, sumAB);
    #endif
  }
  __m128d vlow  = _mm256_castpd256_pd128(sumAB);
  __m128d vhigh = _mm256_extractf128_pd(sumAB, 1);
          vlow  = _mm_add_pd(vlow, vhigh);

  __m128d high64 = _mm_unpackhi_pd(vlow, vlow);
  similarity = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));

  #else
  for( ; i + 3 < vector_size; i += 4 ) {
    double AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
    AB = (searched_array[i+1] - column_array[i+1]);
    similarity += (AB * AB);
    AB = (searched_array[i+2] - column_array[i+2]);
    similarity += (AB * AB);
    AB = (searched_array[i+3] - column_array[i+3]);
    similarity += (AB * AB);
  }
  #endif
  for( ; i < vector_size; ++i ) {
    double AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
  }
  sqlite3_result_double(context, similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_euclidean_distance_similarity_squared_f
// Desc: Calculates the euclidean distance similarity to a BLOB-converted array of floats.
//       Returns the squared result (i.e. doesn't calculate the square root).
// Args: Searched float array BLOB,
//       Compared float array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a squared distance DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_euclidean_distance_similarity_squared_f( sqlite3_context* context,
                                                           int argc,
                                                           sqlite3_value** argv )
{
  if( argc < 2 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally the array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    // Missing one of the required arguments.
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(float);
  }

  const float* searched_array = (const float *)sqlite3_value_blob(argv[0]);
  const float* column_array = (const float *)sqlite3_value_blob(argv[1]);
  float similarity = 0.0f;

  //#pragma GCC ivdep
  int i = 0;
  #ifdef USE_AVX
  __m256 A, B, AB, ABAB, sumAB = _mm256_setzero_ps();
  for( ; i + 7 < vector_size; i += 8 ) {
    A = _mm256_loadu_ps(&searched_array[i]);
    B = _mm256_loadu_ps(&column_array[i]);
    AB = _mm256_sub_ps( A, B );
    #ifdef __AVX2__
    // Fused multiply-add supported (AVX2).
    sumAB = _mm256_fmadd_ps(AB, AB, sumAB );
    #else
    // No fused multiply-add support (AVX).
    ABAB = _mm256_mul_ps(AB, AB);
    sumAB = _mm256_add_ps(ABAB, sumAB);
    #endif
  }
  __m128 vlow   = _mm256_castps256_ps128(sumAB);
  __m128 vhigh  = _mm256_extractf128_ps(sumAB, 1);
         vlow   = _mm_add_ps(vlow, vhigh);
  __m128 high64 = _mm_movehl_ps( vlow, vlow );
  __m128 sum    = _mm_add_ps(vlow, high64);
         sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  similarity = _mm_cvtss_f32(sum);

  #else
  for( ; i + 3 < vector_size; i += 4 ) {
    float AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
    AB = (searched_array[i+1] - column_array[i+1]);
    similarity += (AB * AB);
    AB = (searched_array[i+2] - column_array[i+2]);
    similarity += (AB * AB);
    AB = (searched_array[i+3] - column_array[i+3]);
    similarity += (AB * AB);
  }
  #endif
  for( ; i < vector_size; ++i ) {
    float AB = (searched_array[i] - column_array[i]);
    similarity += (AB * AB);
  }
  sqlite3_result_double(context, (float)similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_dot_product_similarity_d
// Desc: Calculates the dot product similarity to a BLOB-converted array of doubles.
// Args: Searched double array BLOB,
//       Compared double array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a dot product DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_dot_product_similarity_d( sqlite3_context* context,
                                            int argc,
                                            sqlite3_value** argv )
{
  if( argc < 2 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, optionally the array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    // Missing one of the required arguments.
    sqlite3_result_error(context, "One of the given arguments is null.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(double);
  }

  const double* searched_array = (const double *)sqlite3_value_blob(argv[0]);
  const double* column_array = (const double *)sqlite3_value_blob(argv[1]);
  double similarity = 0.0;

  //#pragma GCC ivdep
  int i = 0;
  #ifdef USE_AVX
  __m256d A, B, AB, sumAB = _mm256_setzero_pd();
  for( ; i + 3 < vector_size; i += 4 ) {
    A = _mm256_loadu_pd(&searched_array[i]);
    B = _mm256_loadu_pd(&column_array[i]);
    #ifdef __AVX2__
    sumAB = _mm256_fmadd_pd(A, B, sumAB );
    #else
    AB = _mm256_mul_pd(A, B);
    sumAB = _mm256_add_pd(AB, sumAB);
    #endif
  }
  __m128d vlow  = _mm256_castpd256_pd128(sumAB);
  __m128d vhigh = _mm256_extractf128_pd(sumAB, 1);
          vlow  = _mm_add_pd(vlow, vhigh);

  __m128d high64 = _mm_unpackhi_pd(vlow, vlow);
  similarity = _mm_cvtsd_f64(_mm_add_sd(vlow, high64));

  #else
  for( ; i + 3 < vector_size; i += 4 ) {
    similarity += ((searched_array[i]) * (column_array[i]));
    similarity += ((searched_array[i+1]) * (column_array[i+1]));
    similarity += ((searched_array[i+2]) * (column_array[i+2]));
    similarity += ((searched_array[i+3]) * (column_array[i+3]));
  }
  #endif
  for( ; i < vector_size; ++i ) {
    similarity += ((searched_array[i]) * (column_array[i]));
  }

  sqlite3_result_double(context, similarity);
}


//----------------------------------------------------------------------------------------
// Name: ndvss_dot_product_similarity_f
// Desc: Calculates the dot product similarity to a BLOB-converted array of floats.
// Args: Searched float array BLOB,
//       Compared float array (usually a column) BLOB,
//       Number of dimensions INTEGER
// Returns: Similarity as a dot product DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_dot_product_similarity_f( sqlite3_context* context,
                                            int argc,
                                            sqlite3_value** argv )
{
  if( argc < 2 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "2 arguments needs to be given: searched array, column/compared array, array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ) {
    // Missing one of the required arguments.
    sqlite3_result_error(context, "One of the given arguments is NULL.", -1);
    return;
  }
  int arg1_size_bytes = sqlite3_value_bytes(argv[0]);
  int arg2_size_bytes = sqlite3_value_bytes(argv[1]);
  if( arg1_size_bytes != arg2_size_bytes ) {
    sqlite3_result_error(context, "The arrays are not the same length.", -1);
    return;
  }
  int vector_size = -1;
  if( argc > 2 ) {
    if( sqlite3_value_type(argv[2]) != SQLITE_NULL ) {
      vector_size = sqlite3_value_int(argv[2]);
    }
  }
  if( vector_size < 1 ) {
    vector_size = arg1_size_bytes / sizeof(float);
  }
  const float* searched_array = (const float *)sqlite3_value_blob(argv[0]);
  const float* column_array = (const float *)sqlite3_value_blob(argv[1]);
  float similarity = 0.0f;

  //#pragma GCC ivdep
  int i = 0;
  #ifdef USE_AVX
  __m256 A, B, AB, sumAB = _mm256_setzero_ps();
  for( ; i + 7 < vector_size; i += 8 ) {
    A = _mm256_loadu_ps(&searched_array[i]);
    B = _mm256_loadu_ps(&column_array[i]);
    #ifdef __AVX2__
    sumAB = _mm256_fmadd_ps(A, B, sumAB );
    #else
    AB = _mm256_mul_ps(A, B);
    sumAB = _mm256_add_ps(AB, sumAB);
    #endif
  }
  __m128 vlow   = _mm256_castps256_ps128(sumAB);
  __m128 vhigh  = _mm256_extractf128_ps(sumAB, 1);
         vlow   = _mm_add_ps(vlow, vhigh);
  __m128 high64 = _mm_movehl_ps( vlow, vlow );
  __m128 sum    = _mm_add_ps(vlow, high64);
         sum    = _mm_add_ss(sum, _mm_shuffle_ps( sum, sum, 0x55));
  similarity = _mm_cvtss_f32(sum);

  #else
  for( ; i + 3 < vector_size; i += 4 ) {
    similarity += ((searched_array[i]) * (column_array[i]));
    similarity += ((searched_array[i+1]) * (column_array[i+1]));
    similarity += ((searched_array[i+2]) * (column_array[i+2]));
    similarity += ((searched_array[i+3]) * (column_array[i+3]));
  }
  #endif
  for( ; i < vector_size; ++i ) {
    similarity += ((searched_array[i]) * (column_array[i]));
  }

  sqlite3_result_double(context, (double)similarity);
}



//----------------------------------------------------------------------------------------
// Name: ndvss_dot_product_similarity_str
// Desc: Calculates the dot product similarity between two strings containing arrays of
//       decimal numbers. The first argument (searched array) is cached as a BLOB.
// Args: Searched array TEXT,
//       Compared array (usually a column) TEXT,
//       Number of dimensions INTEGER
// Returns: Similarity as a dot product DOUBLE
//----------------------------------------------------------------------------------------
static void ndvss_dot_product_similarity_str( sqlite3_context* context,
                                                    int argc,
                                                    sqlite3_value** argv )
{
  if( argc < 3 ) {
    // Not enough arguments.
    sqlite3_result_error(context, "3 arguments needs to be given: searched array, column/compared array, array length.", -1);
    return;
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL ||
      sqlite3_value_type(argv[2]) == SQLITE_NULL ) {
    // Missing one of the required argument.
    sqlite3_result_error(context, "One of the given arguments is NULL.", -1);
    return;
  }

  int vector_size = sqlite3_value_int(argv[2]);
  // Parse through the searched value and save it to aux-data for future
  // use.
  void* aux_array = sqlite3_get_auxdata( context, 0 );
  double* comparison_vector = 0;
  if( aux_array == 0 ) {
    comparison_vector = (double*)sqlite3_malloc(vector_size*sizeof(double));
    if(comparison_vector == 0 ) {
      sqlite3_result_error(context, "Out of memory.", -1);
      return;
    }
    char* search_vector = (char*)sqlite3_value_text(argv[0]);
    char* end = search_vector;
    double* index = comparison_vector;
    int i = 0;
    while( end != 0 && i < vector_size ) {
      if (*end == '[' || *end == ']' || *end == ',') {
            // Increment endptr to skip the character
            end++;
            continue;
      }
      *index = strtod(end, &end);
      ++index;
      ++i;
    }//endwhile processing searched string
    sqlite3_set_auxdata(context, 0, comparison_vector, sqlite3_free);
  } else {
    comparison_vector = (double *)aux_array;
  }

  // Start parsing through the second argument and calculate the
  // similarity.
  double similarity = 0.0;
  char* rowvalue_input = (char*)sqlite3_value_text(argv[1]);
  char* end = rowvalue_input;
  double* index = comparison_vector;
  int i = 0;
  while( end != 0 && i < vector_size ) {
    // Skip the JSON-array characters.
    if (*end == '[' || *end == ']' || *end == ',') {
      end++;
      continue;
    }
    double d = strtod(end, &end);
    similarity += (*index * d);
    ++index;
    ++i;
  }//endwhile processing searched string
  sqlite3_result_double(context, similarity);
}


//-----------------------------------------------------------------------------------
// ENTRYPOINT.
//-----------------------------------------------------------------------------------
#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_ndvss_init( sqlite3 *db,
                        char **pzErrMsg,
                        const sqlite3_api_routines *pApi )
{
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function( db,
                                "ndvss_version", // Function name
                                0, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_version, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }

  rc = sqlite3_create_function( db,
                                "ndvss_convert_str_to_array_d", // Function name
                                2, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_convert_str_to_array_d, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_convert_str_to_array_f", // Function name
                                2, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_convert_str_to_array_f, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_cosine_similarity_d", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_cosine_similarity_d, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_cosine_similarity_f", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_cosine_similarity_f, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_euclidean_distance_similarity_d", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_euclidean_distance_similarity_d, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_euclidean_distance_similarity_squared_d", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_euclidean_distance_similarity_squared_d, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_euclidean_distance_similarity_f", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_euclidean_distance_similarity_f, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }
  rc = sqlite3_create_function( db,
                                "ndvss_euclidean_distance_similarity_squared_f", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_euclidean_distance_similarity_squared_f, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }

  rc = sqlite3_create_function( db,
                                "ndvss_dot_product_similarity_d", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_dot_product_similarity_d, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }

  rc = sqlite3_create_function( db,
                                "ndvss_dot_product_similarity_f", // Function name
                                -1, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_dot_product_similarity_f, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }

  rc = sqlite3_create_function( db,
                                "ndvss_dot_product_similarity_str", // Function name
                                3, // Number of arguments
                                SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                0, // *pApp?
                                ndvss_dot_product_similarity_str, // xFunc -> Function pointer
                                0, // xStep?
                                0  // xFinal?
                                );
  if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("%s", sqlite3_errmsg(db));
      return rc;
  }

  return rc;
}

int core_init(const char *unused) {
  int nErr = 0;
  // Register the extension’s init function to auto-load for every new connection
  nErr +=  sqlite3_auto_extension((void*)sqlite3_ndvss_init);
  return nErr ? SQLITE_ERROR : SQLITE_OK;
}
