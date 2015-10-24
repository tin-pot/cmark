/* esisio_int.h */

typedef unsigned char byte;
typedef size_t ref;

#define STACK_CHUNK 2048U

struct esis_stack_ {
  byte *buf;
  size_t lim;
  ref top, mark;
};

void esisStackInit(struct esis_stack_ *p);
void esisStackGrow(struct esis_stack_ *p, size_t n);
ref  esisStackMark(struct esis_stack_ *p, size_t n);
ref  esisStackRelease(struct esis_stack_ *p, ref r);
ref  esisStackPush(struct esis_stack_ *p, const void *v, size_t n);
ref  esisStackPop(struct esis_stack_ *p, void *v, size_t n);

#define P(R_) ( (void*)(pe->S->buf + (R_)) )

#define TOP() (pe->S->top)

#define MARK(N_) \
                                ( ((pe->S->mark + (N_) > pe->S->lim) ? \
                                      esisStackGrow(pe->S, (N_)) : 0), \
             pe->S->top = pe->S->mark, pe->S->mark += (N_), pe->S->top )
                   
#define RELEASE(R_) \
                                                 ( pe->S->mark = (R_), \
   pe->S->top = ( pe->S->top > pe->S->mark) ? pe->S->mark : pe->S->top )

#define PUSH_CHAR(C_) \
                                    ( (pe->S->top + 1U > pe->S->lim) ? \
               esisStackGrow(pe->S, pe->S->top + 1U - pe->S->lim) : 0, \
                                  pe->S->buf[pe->S->top] = (byte)(C_), \
                                                     pe->S->top += 1U, \
                            pe->S->mark = (pe->S->top > pe->S->mark) ? \
                                  pe->S->top : pe->S->mark, pe->S->top )

struct ESIS_ParserStruct {
  struct esis_stack_ S;
};

extern ESIS_Writer ESISAPI
ESIS_WriterCreateInt(FILE *, const ESIS_Char *);

#define ESIS_START_   001U
#define ESIS_END_     002U
#define ESIS_EMPTY_   003U

#define ESIS_CDATA_   010U
#define ESIS_PCDATA_  020U

typedef void (* ESISAPI ESIS_TagWriter)(
                                      FILE             *outputFile,
                                      unsigned          what, 
                                      const ESIS_Char  *elemGI,
                                      const ESIS_Char **atts);
                                      
typedef void (* ESISAPI ESIS_DataWriter)(
                                      FILE             *outputFile,
                                      unsigned    how,
                                      const byte *data,
                                      size_t      len);

struct ESIS_WriterStruct {
  struct esis_stack_    S[1];
  unsigned              n_att;
  ref                   r_att;
  ref                   r_gi;
  ESIS_TagWriter        tagfunc;
  ESIS_DataWriter       datafunc;
  FILE                 *fp;
};
