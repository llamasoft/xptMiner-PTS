// Coded by GigaWatt from bitsharestalk
// Based on SHA512 implementation from OpenSSL with optimizations for GPU
// If you include this in your code, please consider donating!
// BTC: 1E2egHUcLDAmcxcqZqpL18TPLx9Xj1akcV
// XPM: AWHJbwoM67Ez12SHH4pH5DnJKPoMSdvLz2
// PTS: Pstkk1gZCxc4GEwS1eBAykYwVmcubU1P8L


// #define HASHES_PER_WORKER  ( 16 )
#define MAX_MOMENTUM_NONCE ( 1 << 26 )
#define MAX_MOMENTUM_MASK  ( 0x3FFFFFF )
#define SEARCH_SPACE_BITS  ( 50 )
#define SEARCH_SPACE_MASK  ( 0xFFFFFFFFFFFFC000 )
#define BIRTHDAYS_PER_HASH ( 8 )


#ifdef cl_khr_byte_addressable_store
#pragma OPENCL EXTENSION cl_khr_byte_addressable_store : disable
#endif


#define SWAP64(n) as_ulong(as_uchar8(n).s76543210)

#define ROL(x, s)   rotate((ulong)(x), (ulong)(s))
#define ROR(x, s)   ROL((x), (64 - (s)))
// #define ROTR(x,s)	(((x)>>s) | (x)<<(64-s))
#define Sigma0(x)	(ROTR((x),28) ^ ROTR((x),34) ^ ROTR((x),39))
#define Sigma1(x)	(ROTR((x),14) ^ ROTR((x),18) ^ ROTR((x),41))
#define sigma0(x)	(ROTR((x),1)  ^ ROTR((x),8)  ^ ((x)>>7))
#define sigma1(x)	(ROTR((x),19) ^ ROTR((x),61) ^ ((x)>>6))

// #define Ch(x,y,z)	(((x) & (y)) ^ ((~(x)) & (z)))
#define Ch(x,y,z)   bitselect(z, y, x)
#define Maj(x,y,z)	(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define SHUFFLE(a,b,c,d,e,f,g,h,T1,T2) \
{                                      \
    h = g; g = f; f = e; e =  d + T1;  \
    d = c; c = b; b = a; a = T1 + T2;  \
}


#define SHA512_ROUND_00_15(round)                         \
state[round] = SWAP64(input[round]);                      \
T1 = state[round] + h + Sigma1(e) + Ch(e,f,g) + k[round]; \
T2 = Sigma0(a) + Maj(a,b,c);                              \
SHUFFLE(a,b,c,d,e,f,g,h,T1,T2);


#define SHA512_ROUND_16_80(round)                                 \
state[round & 0x0f] +=  sigma0(state[(round+1) & 0x0f])           \
                      + sigma1(state[(round+14)& 0x0f])           \
                      + state[(round+9) & 0x0f];                  \
T1 = state[round & 0x0f] + h + Sigma1(e) + Ch(e,f,g) + k[round];  \
T2 = Sigma0(a) + Maj(a,b,c);                                      \
SHUFFLE(a,b,c,d,e,f,g,h,T1,T2);



__constant ulong k[] = {
    0x428a2f98d728ae22UL, 0x7137449123ef65cdUL, 0xb5c0fbcfec4d3b2fUL, 0xe9b5dba58189dbbcUL,
    0x3956c25bf348b538UL, 0x59f111f1b605d019UL, 0x923f82a4af194f9bUL, 0xab1c5ed5da6d8118UL,
    0xd807aa98a3030242UL, 0x12835b0145706fbeUL, 0x243185be4ee4b28cUL, 0x550c7dc3d5ffb4e2UL,
    0x72be5d74f27b896fUL, 0x80deb1fe3b1696b1UL, 0x9bdc06a725c71235UL, 0xc19bf174cf692694UL,
    0xe49b69c19ef14ad2UL, 0xefbe4786384f25e3UL, 0x0fc19dc68b8cd5b5UL, 0x240ca1cc77ac9c65UL,
    0x2de92c6f592b0275UL, 0x4a7484aa6ea6e483UL, 0x5cb0a9dcbd41fbd4UL, 0x76f988da831153b5UL,
    0x983e5152ee66dfabUL, 0xa831c66d2db43210UL, 0xb00327c898fb213fUL, 0xbf597fc7beef0ee4UL,
    0xc6e00bf33da88fc2UL, 0xd5a79147930aa725UL, 0x06ca6351e003826fUL, 0x142929670a0e6e70UL,
    0x27b70a8546d22ffcUL, 0x2e1b21385c26c926UL, 0x4d2c6dfc5ac42aedUL, 0x53380d139d95b3dfUL,
    0x650a73548baf63deUL, 0x766a0abb3c77b2a8UL, 0x81c2c92e47edaee6UL, 0x92722c851482353bUL,
    0xa2bfe8a14cf10364UL, 0xa81a664bbc423001UL, 0xc24b8b70d0f89791UL, 0xc76c51a30654be30UL,
    0xd192e819d6ef5218UL, 0xd69906245565a910UL, 0xf40e35855771202aUL, 0x106aa07032bbd1b8UL,
    0x19a4c116b8d2d0c8UL, 0x1e376c085141ab53UL, 0x2748774cdf8eeb99UL, 0x34b0bcb5e19b48a8UL,
    0x391c0cb3c5c95a63UL, 0x4ed8aa4ae3418acbUL, 0x5b9cca4f7763e373UL, 0x682e6ff3d6b2b8a3UL,
    0x748f82ee5defb2fcUL, 0x78a5636f43172f60UL, 0x84c87814a1f0ab72UL, 0x8cc702081a6439ecUL,
    0x90befffa23631e28UL, 0xa4506cebde82bde9UL, 0xbef9a3f7b2c67915UL, 0xc67178f2e372532bUL,
    0xca273eceea26619cUL, 0xd186b8c721c0c207UL, 0xeada7dd6cde0eb1eUL, 0xf57d4f7fee6ed178UL,
    0x06f067aa72176fbaUL, 0x0a637dc5a2c898a6UL, 0x113f9804bef90daeUL, 0x1b710b35131c471bUL,
    0x28db77f523047d84UL, 0x32caab7b40c72493UL, 0x3c9ebe0a15c9bebcUL, 0x431d67c49c100d4cUL,
    0x4cc5d4becb3e42b6UL, 0x597f299cfc657e2aUL, 0x5fcb6fab3ad6faecUL, 0x6c44198c4a475817UL
};

__constant ulong IV[] = {
    0x6a09e667f3bcc908UL,
    0xbb67ae8584caa73bUL,
    0x3c6ef372fe94f82bUL,
    0xa54ff53a5f1d36f1UL,
    0x510e527fade682d1UL,
    0x9b05688c2b3e6c1fUL,
    0x1f83d9abfb41bd6bUL,
    0x5be0cd19137e2179UL
};


// Different ways of accessing the same 128-byte chunk of data
// Beware: endian-ness gets wonky when alternating between sizes
// Example: if b8[0] is 0xDE and b8[1] is 0xAD
//          then b16[0] is 0xADDE (not 0xDEAD)
typedef union { ulong b64[16]; uint b32[32]; uchar b8[128]; } hash_input;


__kernel void hash_nonce(constant uint *mid_hash, global ulong *hash_list, global uint *hash_list) {

    uint seed_nonce = HASHES_PER_WORKER * BIRTHDAYS_PER_HASH * get_global_id(0);
    uint cur_nonce = seed_nonce;
    // if (seed_nonce >= MAX_MOMENTUM_NONCE) { return; }


    //////////  BEGIN SHA512 //////////
    hash_input H;

    // H.b32[0] is reserved for nonce
    H.b32[1] = mid_hash[0];
    H.b32[2] = mid_hash[1];
    H.b32[3] = mid_hash[2];
    H.b32[4] = mid_hash[3];
    H.b32[5] = mid_hash[4];
    H.b32[6] = mid_hash[5];
    H.b32[7] = mid_hash[6];
    H.b32[8] = mid_hash[7];
    H.b32[9] = 0x80; // High 1 bit to mark end of input

    // Fill the mid-section with 0-bytes
    H.b64[ 5] = 0; H.b64[ 6] = 0;
    H.b64[ 7] = 0; H.b64[ 8] = 0;
    H.b64[ 9] = 0; H.b64[10] = 0;
    H.b64[11] = 0; H.b64[12] = 0;
    H.b64[13] = 0; H.b64[14] = 0;

    // Append length in *bits* as big endian integer
    H.b64[15] = 0x2001000000000000; // 0x2001 = SWAP64(288) = 9 chunks * 32bits


    // Variables used in processing
    ulong a, b, c, d, e, f, g, h, T1, T2;
    ulong state[16];
    ulong *input = (ulong *)&H.b64;


    //////////  BEGIN SHA512 //////////
    for (int pass = 0; pass < HASHES_PER_WORKER; ++pass) {
        H.b32[0] = cur_nonce;
        a = 0x6a09e667f3bcc908UL;
        b = 0xbb67ae8584caa73bUL;
        c = 0x3c6ef372fe94f82bUL;
        d = 0xa54ff53a5f1d36f1UL;
        e = 0x510e527fade682d1UL;
        f = 0x9b05688c2b3e6c1fUL;
        g = 0x1f83d9abfb41bd6bUL;
        h = 0x5be0cd19137e2179UL;

        // Forgive me father, for I have sinned.
        SHA512_ROUND_00_15( 0); SHA512_ROUND_00_15( 1); SHA512_ROUND_00_15( 2); SHA512_ROUND_00_15( 3);
        SHA512_ROUND_00_15( 4); SHA512_ROUND_00_15( 5); SHA512_ROUND_00_15( 6); SHA512_ROUND_00_15( 7);
        SHA512_ROUND_00_15( 8); SHA512_ROUND_00_15( 9); SHA512_ROUND_00_15(10); SHA512_ROUND_00_15(11);
        SHA512_ROUND_00_15(12); SHA512_ROUND_00_15(13); SHA512_ROUND_00_15(14); SHA512_ROUND_00_15(15);

        SHA512_ROUND_16_80(16); SHA512_ROUND_16_80(17); SHA512_ROUND_16_80(18); SHA512_ROUND_16_80(19);
        SHA512_ROUND_16_80(20); SHA512_ROUND_16_80(21); SHA512_ROUND_16_80(22); SHA512_ROUND_16_80(23);
        SHA512_ROUND_16_80(24); SHA512_ROUND_16_80(25); SHA512_ROUND_16_80(26); SHA512_ROUND_16_80(27);
        SHA512_ROUND_16_80(28); SHA512_ROUND_16_80(29); SHA512_ROUND_16_80(30); SHA512_ROUND_16_80(31);
        SHA512_ROUND_16_80(32); SHA512_ROUND_16_80(33); SHA512_ROUND_16_80(34); SHA512_ROUND_16_80(35);
        SHA512_ROUND_16_80(36); SHA512_ROUND_16_80(37); SHA512_ROUND_16_80(38); SHA512_ROUND_16_80(39);
        SHA512_ROUND_16_80(40); SHA512_ROUND_16_80(41); SHA512_ROUND_16_80(42); SHA512_ROUND_16_80(43);
        SHA512_ROUND_16_80(44); SHA512_ROUND_16_80(45); SHA512_ROUND_16_80(46); SHA512_ROUND_16_80(47);
        SHA512_ROUND_16_80(48); SHA512_ROUND_16_80(49); SHA512_ROUND_16_80(50); SHA512_ROUND_16_80(51);
        SHA512_ROUND_16_80(52); SHA512_ROUND_16_80(53); SHA512_ROUND_16_80(54); SHA512_ROUND_16_80(55);
        SHA512_ROUND_16_80(56); SHA512_ROUND_16_80(57); SHA512_ROUND_16_80(58); SHA512_ROUND_16_80(59);
        SHA512_ROUND_16_80(60); SHA512_ROUND_16_80(61); SHA512_ROUND_16_80(62); SHA512_ROUND_16_80(63);
        SHA512_ROUND_16_80(64); SHA512_ROUND_16_80(65); SHA512_ROUND_16_80(66); SHA512_ROUND_16_80(67);
        SHA512_ROUND_16_80(68); SHA512_ROUND_16_80(69); SHA512_ROUND_16_80(70); SHA512_ROUND_16_80(71);
        SHA512_ROUND_16_80(72); SHA512_ROUND_16_80(73); SHA512_ROUND_16_80(74); SHA512_ROUND_16_80(75);
        SHA512_ROUND_16_80(76); SHA512_ROUND_16_80(77); SHA512_ROUND_16_80(78); SHA512_ROUND_16_80(79);

        a += 0x6a09e667f3bcc908UL;
        b += 0xbb67ae8584caa73bUL;
        c += 0x3c6ef372fe94f82bUL;
        d += 0xa54ff53a5f1d36f1UL;
        e += 0x510e527fade682d1UL;
        f += 0x9b05688c2b3e6c1fUL;
        g += 0x1f83d9abfb41bd6bUL;
        h += 0x5be0cd19137e2179UL;
        //////////  END SHA512 //////////


        hash_list[cur_nonce + 0] = (SWAP64(a) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 1] = (SWAP64(b) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 2] = (SWAP64(c) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 3] = (SWAP64(d) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 4] = (SWAP64(e) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 5] = (SWAP64(f) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 6] = (SWAP64(g) & SEARCH_SPACE_MASK);
        hash_list[cur_nonce + 7] = (SWAP64(h) & SEARCH_SPACE_MASK);
        
        nonce_list[cur_nonce + 0] = cur_nonce + 0;
        nonce_list[cur_nonce + 1] = cur_nonce + 1;
        nonce_list[cur_nonce + 2] = cur_nonce + 2;
        nonce_list[cur_nonce + 3] = cur_nonce + 3;
        nonce_list[cur_nonce + 4] = cur_nonce + 4;
        nonce_list[cur_nonce + 5] = cur_nonce + 5;
        nonce_list[cur_nonce + 6] = cur_nonce + 6;
        nonce_list[cur_nonce + 7] = cur_nonce + 7;

        cur_nonce += BIRTHDAYS_PER_HASH;
    }

    return;
}


__kernel void bitonicSort(global ulong* hashes,    global uint* nonces,
                          local  ulong* hash_temp, local  uint* nonce_temp)
{
    int i = get_local_id(0);
    int wgs = get_local_size(0);

    ulong left_hash,  right_hash;
    uint  left_nonce, right_nonce;

    // Move input data to block's start location
    int offset = get_group_id(0) * wgs;
    hashes += offset;
    nonces += offset;

    // Load data and copy into temp storage
    left_hash  = hashes[i];
    left_nonce = nonces[i];
    hash_temp[i]  = left_hash;
    nonce_temp[i] = left_nonce;
    
    
    barrier(CLK_LOCAL_MEM_FENCE);


    // Loop on sorted sequence length
    for (int length = 1; length < wgs; length <<= 1)
    {
        // Determine direction of sort (0 = ascending, 1 = descending)
        bool direction = ( (i & (length << 1)) != 0 );

        // Loop on comparison distance (between keys)
        for (int inc = length; inc > 0; inc >>= 1)
        {
            // Get "sibling" index
            int j = i ^ inc;
            
            // Load "sibling" data
            right_hash  = hash_temp[j];
            right_nonce = nonce_temp[j];
            
            // If the datum should be swapped (based on data, index, and sort order)
            if ((right_hash < left_hash) ^ (j < i) ^ direction) {
                left_hash  = right_hash;
                left_nonce = right_nonce;
            }

            // Save the results
            barrier(CLK_LOCAL_MEM_FENCE);            
            hash_temp[i]  = left_hash;
            nonce_temp[i] = left_nonce;
            barrier(CLK_LOCAL_MEM_FENCE);
        }
    }
    
    // Write output
    hashes[i] = left_hash;
    nonces[i] = left_nonce;
}
