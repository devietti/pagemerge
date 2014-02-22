#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
//#include <time.h> // for clock_gettime()
#include <sys/time.h> // for gettimeofday()

#include <emmintrin.h> // for __m128i
#include <smmintrin.h>

#define NUM_PAGES 3000
#define PAGE_SIZE (2<<12) // 4KB

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

char* LATEST [NUM_PAGES];
char* LOCAL [NUM_PAGES];
char* REF [NUM_PAGES];

void allocate(char** pageArray) {
  memset( pageArray, 0, sizeof(char*) * NUM_PAGES);
  // allocate pages in random order
  for (int i = 0; i < NUM_PAGES; i++) {
    int j = rand() % NUM_PAGES;
    while ( pageArray[j] != NULL ) {
      j = (j+1) % NUM_PAGES;
    }
    pageArray[j] = (char*) calloc( 1, PAGE_SIZE );
    assert( pageArray[j] != NULL );
  }

  /* // all pages should be filled in */
  /* for (int i = 0; i < NUM_PAGES; i++) { */
  /*   assert( pageArray[i] != NULL ); */
  /* } */
}

void initialize() {
  for (int i = 0; i < NUM_PAGES; i++) {
    if ( rand() % 100 >= PERCENT_DIFF_PAGES ) { // latest == ref
      // fill page with random data
      char r = rand() % 256;
      for (int j = 0; j < PAGE_SIZE; j++) {
        LATEST[i][j] = LOCAL[i][j] = REF[i][j] = r;
      }

    } else { // latest != ref
      int dwpp = (rand() % MAX_DIFF_WORDS_PER_PAGE) + 1;
      for (int j = 0; j < dwpp; j++) {
        int* page = (int*) LATEST[i];
        int offset = rand() % (PAGE_SIZE / sizeof(int));
        page[offset] = rand();
      }
    }
  }

  // flush caches
  const int SIZE = 32<<20; // 32MB
  char* buf = (char*) calloc( 1, SIZE );
  for (int i = 0; i < SIZE; i++) {
    buf[i] += 2;
  }
}

void validate() {
  for (int i = 0; i < NUM_PAGES; ++i) {
    int diff = memcmp( &LATEST[i][0], &LOCAL[i][0], PAGE_SIZE );
    assert( !diff );
  }
}

__attribute__((optimize("unroll-loops")))
void merge() {
#if defined(SSE_MERGE) || defined(SSE_MERGE_UNROLL)
  __m128i isTrue = _mm_set1_epi16(0xFFFF);
#endif

  for (int i = 0; i < NUM_PAGES; ++i) {
    //merge in everything thats different between the ref and the latest committed page (that we haven't touched)
    
#ifdef PREFETCH
    for (int pages = 1; pages <= PREFETCH_PAGES; pages++) {
      for (int bpp = 0; bpp < PREFETCH_BYTES_PER_PAGE; bpp++) {
        __builtin_prefetch( &LATEST[i+pages][bpp], 0/*read*/, 3/*high temporal locality*/ );
        __builtin_prefetch( &REF[i+pages][bpp], 0/*read*/, 3/*high temporal locality*/ );
	// don't prefetch LOCAL since we generally don't need it
        //__builtin_prefetch( &LOCAL[i+pages][bpp], 1/*write*/, 3/*high temporal locality*/ );
      }
    }
#endif

#ifdef BYTE_MERGE
    const char* latest = LATEST[i];
    const char* ref = REF[i];
    char* local = LOCAL[i];
    for (int j = 0; j < PAGE_SIZE; ++j) {
      if ( unlikely(latest[j]!=ref[j] && local[j]==ref[j]) ){
        local[j] = latest[j];
      }
    }
#endif
#ifdef WORD_MERGE
    const uint64_t* latest = (const uint64_t*) LATEST[i];
    const uint64_t* ref = (const uint64_t*) REF[i];
    uint64_t* local = (uint64_t*) LOCAL[i];

    for (int j = 0; j < (PAGE_SIZE/sizeof(uint64_t)); ++j) {

      // check for diff at word granularity first
      if ( unlikely(latest[j]!=ref[j]) ) {
        if ( local[j] == ref[j] ) {
          local[j] = latest[j];

        } else {
          // have to do byte-wise comparison
          const char* latestChar = (const char*) latest[j];
          const char* refChar = (const char*) ref[j];
          char* localChar = (char*) local[j];
          for ( int k = 0; k < sizeof(uint64_t); k++ ) {
            if ( latestChar[k] != refChar[k] && localChar[k] == refChar[k] ) {
              localChar[k] = latestChar[k];
            }
          }
        }
      }

    }
#endif
#ifdef SSE_MERGE 
    const char* latestP = LATEST[i];
    const char* refP = REF[i];
    char* localP = LOCAL[i];

    for (int j = 0; j < PAGE_SIZE; j += sizeof(__m128i)) {
      __m128i latest = _mm_load_si128( (__m128i*) (latestP+j) );
      __m128i ref = _mm_load_si128( (__m128i*) (refP+j) );
      __m128i latEqRef = _mm_cmpeq_epi8(latest, ref); // if latest == ref, latref is all ones

      if ( unlikely(!_mm_testc_si128(latEqRef, isTrue)) ) {
        // some bytes differ
	__m128i local = _mm_load_si128( (__m128i*) (localP+j) );
        __m128i localEqRef = _mm_cmpeq_epi8(local, ref);
        if ( _mm_testc_si128(localEqRef, isTrue) ) {
          // local == ref
          _mm_stream_si128( (__m128i*) (localP+j), latest );
        } else {
          // (~latref) & localref, bytes where lat!=ref && local==ref
          __m128i latestMask = _mm_andnot_si128( latEqRef, localEqRef );
          // new = (latestMask & latest) | (~latestMask & local);
          __m128i latestBytes = _mm_and_si128(latestMask, latest);
          __m128i localBytes = _mm_andnot_si128(latestMask, local);
          latestBytes = _mm_or_si128(latestBytes, localBytes);
          _mm_stream_si128( (__m128i*) (localP+j), latestBytes );
        }
      }
    }
#endif
#ifdef SSE_MERGE_NOBRANCH
    for (int j = 0; j < PAGE_SIZE; j += sizeof(__m128i)) {
      __m128i latest = _mm_load_si128( (__m128i*) &LATEST[i][j] );
      __m128i ref = _mm_load_si128( (__m128i*) &REF[i][j] );
      __m128i local = _mm_load_si128( (__m128i*) &LOCAL[i][j] );
      __m128i latref = _mm_cmpeq_epi8(latest, ref); // if latest == ref, latref is all ones
      __m128i tmp = _mm_cmpeq_epi8(local, ref);
      latref = _mm_andnot_si128( latref, tmp ); // (~latref) & localref
      // update = (latref & latest) | (~latref & local);
      tmp = _mm_and_si128(latref, latest);
      __m128i localBytes = _mm_andnot_si128(latref, local);
      tmp = _mm_or_si128(tmp, localBytes);
      _mm_stream_si128( (__m128i*) &LOCAL[i][j], tmp );
    }
#endif
#ifdef SSE_MERGE_UNROLL
    // manually unroll this loop since gcc won't do it; ugh
    const char* latestP = LATEST[i];
    const char* refP = REF[i];
    char* localP = LOCAL[i];

    for (int j = 0; j < PAGE_SIZE; j += sizeof(__m128i)) {
      __m128i latest = _mm_load_si128( (__m128i*) (latestP+j) );
      __m128i ref = _mm_load_si128( (__m128i*) (refP+j) );
      __m128i latEqRef = _mm_cmpeq_epi8(latest, ref); // if latest == ref, latref is all ones

      if ( unlikely(!_mm_testc_si128(latEqRef, isTrue)) ) {
        // some bytes differ
	__m128i local = _mm_load_si128( (__m128i*) (localP+j) );
        __m128i localEqRef = _mm_cmpeq_epi8(local, ref);
        if ( _mm_testc_si128(localEqRef, isTrue) ) {
          // local == ref
          _mm_stream_si128( (__m128i*) (localP+j), latest );
        } else {
          // (~latref) & localref, bytes where lat!=ref && local==ref
          __m128i latestMask = _mm_andnot_si128( latEqRef, localEqRef );
          // new = (latestMask & latest) | (~latestMask & local);
          __m128i latestBytes = _mm_and_si128(latestMask, latest);
          __m128i localBytes = _mm_andnot_si128(latestMask, local);
          latestBytes = _mm_or_si128(latestBytes, localBytes);
          _mm_stream_si128( (__m128i*) (localP+j), latestBytes );
        }
      }

      j += sizeof(__m128i);
      latest = _mm_load_si128( (__m128i*) (latestP+j) );
      ref = _mm_load_si128( (__m128i*) (refP+j) );
      latEqRef = _mm_cmpeq_epi8(latest, ref); // if latest == ref, latref is all ones

      if ( unlikely(!_mm_testc_si128(latEqRef, isTrue)) ) {
        // some bytes differ
	__m128i local = _mm_load_si128( (__m128i*) (localP+j) );
        __m128i localEqRef = _mm_cmpeq_epi8(local, ref);
        if ( _mm_testc_si128(localEqRef, isTrue) ) {
          // local == ref
          _mm_stream_si128( (__m128i*) (localP+j), latest );
        } else {
          // (~latref) & localref, bytes where lat!=ref && local==ref
          __m128i latestMask = _mm_andnot_si128( latEqRef, localEqRef );
          // new = (latestMask & latest) | (~latestMask & local);
          __m128i latestBytes = _mm_and_si128(latestMask, latest);
          __m128i localBytes = _mm_andnot_si128(latestMask, local);
          latestBytes = _mm_or_si128(latestBytes, localBytes);
          _mm_stream_si128( (__m128i*) (localP+j), latestBytes );
        }
      }

      j += sizeof(__m128i);
      latest = _mm_load_si128( (__m128i*) (latestP+j) );
      ref = _mm_load_si128( (__m128i*) (refP+j) );
      latEqRef = _mm_cmpeq_epi8(latest, ref); // if latest == ref, latref is all ones

      if ( unlikely(!_mm_testc_si128(latEqRef, isTrue)) ) {
        // some bytes differ
	__m128i local = _mm_load_si128( (__m128i*) (localP+j) );
        __m128i localEqRef = _mm_cmpeq_epi8(local, ref);
        if ( _mm_testc_si128(localEqRef, isTrue) ) {
          // local == ref
          _mm_stream_si128( (__m128i*) (localP+j), latest );
        } else {
          // (~latref) & localref, bytes where lat!=ref && local==ref
          __m128i latestMask = _mm_andnot_si128( latEqRef, localEqRef );
          // new = (latestMask & latest) | (~latestMask & local);
          __m128i latestBytes = _mm_and_si128(latestMask, latest);
          __m128i localBytes = _mm_andnot_si128(latestMask, local);
          latestBytes = _mm_or_si128(latestBytes, localBytes);
          _mm_stream_si128( (__m128i*) (localP+j), latestBytes );
        }
      }

    }
#endif


  }
}

int main(int argc, char** argv) {
  
  allocate(LATEST);
  allocate(LOCAL);
  allocate(REF);
  initialize();

  int bad;
  struct timeval before, after;
  bad = gettimeofday( &before, NULL );
  assert( !bad );

  merge();
  
  bad = gettimeofday( &after, NULL );
  assert( !bad );

  //validate();
  
  long elapsed_usec = ((after.tv_sec - before.tv_sec)*1000000) + (after.tv_usec - before.tv_usec);
  printf( "elapsed: %lu us \n", elapsed_usec );

}
