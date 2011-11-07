//Author: Ankudinov Alexander

//for working with large files
#define _FILE_OFFSET_BITS 64

#ifndef BUF_SIZE
#define BUF_SIZE 262144
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#ifdef DEBUG
#define DBG(fmt, a...) fprintf(stderr, "In %s on line %d: " fmt "\n", __FUNCTION__, __LINE__, ## a)
#else
#define DBG(fmt, a...)
#endif

#define INFO(fmt, a...) fprintf(stderr, fmt, ## a)

using namespace std;

const char HEADER[] = "HFMN";
const short TSIZE = 256 * 2 - 1;

struct node_t {
    short v;
    unsigned long long k; // count in file
    short l, r, p; 
};

struct code_t {
    short k;
    char c[33];
};

code_t hcode[256];
node_t htree[TSIZE];
char buf[BUF_SIZE];

struct setcomp {
  bool operator() (const short& a, const short& b) const
  {
      if (htree[a].k != htree[b].k)
          return htree[a].k < htree[b].k;
      else
          return a < b;
  }
};

set<short, setcomp> mintree;

void init_htree()
{
    memset(htree, 0, sizeof(htree));
    for (short i = 0; i < TSIZE; ++i)
        htree[i].v = i;
    htree[TSIZE - 1].p = TSIZE - 1;
}

inline void write_bit(char* &ptr, char &pos, const char & bit)
{
    *ptr |= bit << pos;
    ++pos;
    if (pos >= 8) {
        pos = 0;
        ++ptr;
    }
}

inline char read_bit(char* &ptr, char &pos)
{
    char res = (*ptr & 1 << pos) >> pos; 
    ++pos;
    if (pos >= 8) {
        pos = 0;
        ++ptr;
    }
    return res;
}

void hcode_rec(short v, char* &ptr, char &pos, short &k)
{
    if (v == TSIZE - 1)
        return;
    short t = htree[v].p;
    hcode_rec(htree[v].p, ptr, pos, k);
    write_bit(ptr, pos, (htree[t].l == v) ? 0 : 1);
    ++k;
}

int compress(FILE *in, FILE *out)
{
    off64_t fsize;

    // counting bytes in file
    INFO("Preparing... ");
    while(!feof(in)) {
        int c = fgetc(in);
        ++htree[c].k;
    }
    fsize = ftell(in);
    rewind(in);
    INFO("OK\n");

    // building haffman tree
    INFO("Building haffman tree... ");
    for (short i = 0; i < 256; ++i)
        mintree.insert(i);

    for (short i = 256; i < TSIZE; ++i) {
        node_t & t = htree[i];

        t.l = *mintree.begin();
        mintree.erase(mintree.begin());
        t.r = *mintree.begin();
        mintree.erase(mintree.begin());

        t.k = htree[t.l].k + htree[t.r].k;
        htree[t.l].p = htree[t.r].p = i;

        mintree.insert(i);
    }
    INFO("OK\n");

    // precalc haffman bit codes for each byte
    for (short i = 0; i < 256; ++i) {
        char *ptr = hcode[i].c;
        char pos = 0;
        short & k = hcode[i].k;
        hcode_rec(i, ptr, pos, k);
    }

    // write header with size information and Haffman tree
    fputs(HEADER, out);
    fwrite(&fsize, sizeof(fsize), 1, out);
    for (short i = 256; i < TSIZE; ++i) {
        fwrite(&htree[i].l, sizeof(htree[i].l), 1, out);
        fwrite(&htree[i].r, sizeof(htree[i].r), 1, out);
    }

    // reading input for second time and encoding file
    // using buffer buf
    INFO("Writing resulting archive... ");
    
    char *buf_ptr = buf;
    char buf_pos = 0;

    while (!feof(in)) {
        int c = fgetc(in);
        char *ptr = hcode[c].c;
        char pos = 0;
        for (short i = 0; i < hcode[c].k; ++i) {
            write_bit(buf_ptr, buf_pos, read_bit(ptr, pos));
            if (buf_ptr >= buf + BUF_SIZE) {
                fwrite(buf, sizeof(char), BUF_SIZE, out);
                memset(buf, 0, sizeof(buf));
                buf_ptr = buf;
            }
        }
    }
    if (buf_pos > 0) ++buf_ptr;
    fwrite(buf, sizeof(char), buf_ptr - buf, out);
    
    fclose(in);
    fclose(out);
    
    INFO("DONE\n");
}

/*TODO: add checks for eof, and other problems with archive*/
int extract(FILE *in, FILE *out)
{
    off64_t fsize;
    char tmp_s[5];
    
    // reading first 4 bytes and checking them
    memset(tmp_s, 0, sizeof(tmp_s));
    fgets(tmp_s, 5, in);
    if (strcmp(tmp_s, HEADER) != 0) {
        INFO("Not a haffman archive!\n");
        return -1;
    }

    // reading filesize and haffman table
    fread(&fsize, sizeof(fsize), 1, in);
    for (short i = 256; i < TSIZE; ++i) {
        fread(&htree[i].l, sizeof(htree[i].l), 1, in);
        fread(&htree[i].r, sizeof(htree[i].r), 1, in);
    }

    // set output buffering
    setvbuf(out, NULL, _IOLBF, BUF_SIZE);

    off64_t tsize = 0;
    short v = TSIZE - 1;
    while (tsize < fsize && !feof(in)) {
        int count = fread(buf, sizeof(char), BUF_SIZE, in);
        char *ptr = buf;
        char pos = 0;
        for (int i = 0; i < 8 * count; ++i) {
            if (read_bit(ptr, pos) == 0)
                v = htree[v].l;
            else
                v = htree[v].r;
            if (v < 256) {
                fputc(v, out); ++tsize;
                if (tsize >= fsize) break;
                v = TSIZE - 1;
            }
        }
    }

    fclose(in);
    fclose(out);
}

void help(char *name)
{
    printf("Usage: %s [-x] file.in file.out\n", name);
    printf("file.in must exist\n", name);
    exit(EXIT_FAILURE);
}

#define FOR(a,b) for( int (a) = 0; (a) < (b); ++ (a))
int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4)
        help(argv[0]);
    if (argc == 4 && strcmp(argv[1], "-x") != 0 )
        help(argv[0]);
    
    FILE *in, *out;
    in  = fopen(argv[argc - 2], "rb");
    out = fopen(argv[argc - 1], "wb");
    if (in == NULL || out == NULL) {
        printf("Can`t open files\n\n");
        help(argv[0]);
    }

    init_htree();
    if (argc == 3)
        return compress(in, out);
    else
        return extract(in, out);

    return 0;
}

