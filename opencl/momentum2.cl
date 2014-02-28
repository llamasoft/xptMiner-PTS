// By grouping hashes into buckets, we can eliminate the need to store certain bits of data.
// For example, if we divide the hash_list into 2^8 (256) buckets, then we no longer
//   are required to store 8 bits of the hash.  The fact that it's in a given bucket means
//   that those missing bits are known.
// By using 2^12 buckets, we reduce the required hash storage length to 38 bits.
// This allows us to fit the nonce (26 bits) and the hash in one 64-bit element.
// The downside to this trade-off is that we now need 12 extra variables to keep
//   track of the bucket sizes.  Additionally, it's possible for data to be discarded
//   if a bucket fills up.

// For this application, the number of buckets is supplied in LOG2 format, meaning for
//   a value of N, we will create 2^N buckets and require an N item index_list.
// Because we need to fit the hash and nonce in a 64-bit element, the lowest usable
//   value for N is 12.  Furthermore, N must be less than 26 for obvious reasons.
// In this implimentation, the 64-bit elements will have the following structure:
//   High 26 bits: nonce
//   Low 38 bits: hash (we don't need all 38 bits, but they're nice to have)

#define MAX_NONCE_LOG2      ( 26 )
#define MAX_NONCE           ( 1 << MAX_MOMENTUM_NONCE_LOG2 )
#define NUM_BUCKETS_LOG2    ( 12 )
#define NUM_BUCKETS         ( 1 << NUM_BUCKETS_LOG2 )
#define BUCKET_SIZE         ( 1 << (MAX_NONCE_LOG2 - NUM_BUCKETS_LOG2) )

// 14 = 64 - 50 (MAX_MOMENTUM_HASH)
#define HASH_TO_BDAY(h)     ( (h) >> 14 )
#define BDAY_TO_BUCKET(b)   ( (b) % (1 << (NUM_BUCKETS_LOG2)) )
#define COMPRESS_BDAY(b)    ( (b) >> 12 )
#define MAKE_DATA(b, n)     ( ((n) << 38) | COMPRESS_BDAY(b) )
#define EXTRACT_NONCE(d)    ( (d) >> 38 )
#define EXTRACT_BDAY(d)     ( (d) & 0x3FFFFFFFFF )

// Same as BUCKET_NUM * BUCKET_SIZE
#define BUCKET_TO_OFFSET(b) ( b << (MAX_NONCE_LOG2 - NUM_BUCKETS_LOG2) )