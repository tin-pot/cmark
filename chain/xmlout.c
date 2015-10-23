/* xmlout.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NELEM 19

#define EL_EMPTY   0001
#define EL_CDATA   0002
#define EL_PCDATA  0004
#define EL_OMIT    0010

#define ATTR_MAX    128
#define BUF_SIZE   4096
#define NAME_SIZE   128

#define RE_CHAR '\n'
#define RS_CHAR '\r'

struct trans {
  const char *ingi;
  const char *outgi;
  unsigned flags;
};

struct trans xmltab[NELEM] = {
  { "none",           NULL,   EL_OMIT                    },
  { "document",       NULL,   0                          },
  { "block_quote",    NULL,   0                          },
  { "list",           NULL,   0                          },
  { "item",           NULL,   0                          },
  { "code_block",     NULL,   EL_PCDATA,                 },
  { "html",           NULL,   EL_CDATA | EL_OMIT,        },
  { "paragraph",      NULL,   0,                         },
  { "header",         NULL,   0,                         },
  { "hrule",          NULL,   EL_EMPTY,                  },
  { "text",           NULL,   EL_PCDATA | EL_OMIT,       },
  { "softbreak",      NULL,   EL_EMPTY,                  },
  { "linebreak",      NULL,   EL_EMPTY,                  },
  { "code",           NULL,   EL_PCDATA,                 },
  { "inline_html",    NULL,   EL_CDATA | EL_OMIT,        },
  { "emph",           NULL,   0,                         },
  { "strong",         NULL,   0,                         },
  { "link",           NULL,   0,                         },
  { "image",          NULL,   0,                         },
};

int cmp(const void *lhs, const void *rhs)
{
    const struct trans *tl = lhs, *tr = rhs;
    return strcmp(tl->ingi, tr->ingi);
}

const struct trans *find_trans(const char *name)
{
  struct trans key;
  
  key.ingi = name;
  return bsearch(&key, xmltab, NELEM, sizeof xmltab[0], cmp);
}

void trans_pi()
{
  int ch;
  
  putchar('<');
  putchar('?');
  while ((ch = getchar()) != EOF) {
    if (ch == '\n')
      break;
    putchar(ch);
  }
  putchar('?');
  putchar('>');
  putchar('\n');
}

int decode()
{
  int ch = getchar();
  if (ch == '\n')
    return EOF;
  else if (ch != '\\')
    return ch;
  else switch (ch = getchar()) {
    unsigned num, dig;
    
    case EOF:
    case '\n': return EOF;
    
    case 'n': return RE_CHAR;
    
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
      num = ch - '0';
      
      ch = getchar();
      if (ch == EOF || ch == '\n')
        return EOF;
      dig = ch - '0';
      if (0 <= dig && dig <= 7)
        num = num * 8 + dig;
      else
        return EOF;
        
      ch = getchar();
      if (ch == EOF || ch == '\n')
        return EOF;
      dig = ch - '0';
      if (0 <= dig && dig <= 7)
        num = num * 8 + dig;
      else
        return EOF;
      return (num == '\012') ? RS_CHAR : num;
        
    default:
      return EOF;
  }
}

void cdata()
{
  int ch;
  
  while ((ch = decode()) != EOF)
    if (ch != RS_CHAR) putchar(ch);
}

void pcdata()
{
  int ch;
  
  while ((ch = decode()) != EOF) {
    switch (ch) {
    case '<':     printf("&lt;");   break;
    case '&':     printf("&amp;");  break;
    case '>':     printf("&gt;");   break;
    case '"':     printf("&quot;"); break;
    case RS_CHAR: /* ignored */     break;
    case RE_CHAR: putchar('\n');    break;
    default:      putchar(ch);
    }
  }
}

char *store_attr(char *buf, char **attrn, char **attrv)
{
  int ch;

  *attrn = buf;
  while ((ch = getchar()) != EOF) {
    if (ch == ' ' || ch == '\n')
      break;
    *buf++ = ch;
  }   
  *buf++ = '\0';

  *attrv = NULL;
  if (ch == ' ') {
    while ((ch = getchar()) != EOF) {
      if (ch == ' ' || ch == '\n')
        break;
    }
    if (ch == ' ') {
      *attrv = buf;
      while ((ch = getchar()) != EOF) {
        if (ch == '\n')
          break;
        *buf++ = ch;
      }
      *buf++ = '\0';
    }
  }
  return buf;
}

#define forget_attr() do { attrc = 0; bufp = buf; } while (0) 

void store_name(char *name)
{
  int ch;
  while ((ch = getchar()) != EOF) {
    if (ch == '\n')
      break;
    *name++ = ch;
  }
  *name = '\0';
}

void translate(const struct trans tab[])
{
  int ch;
  int attrc;
  char *attrn[ATTR_MAX];
  char *attrv[ATTR_MAX];
  char buf[BUF_SIZE], *bufp;
  char name[NAME_SIZE];
  const char *outname;
  const struct trans *tp;
  unsigned flags;
  
  forget_attr();
  
  while ((ch = getchar()) != EOF) {
    switch (ch) {
      case '?':
        trans_pi();
        break;
      case 'A':
        bufp = store_attr(bufp, attrn+attrc, attrv + attrc);
        ++attrc;
        break;
      case '(':
        store_name(name);
        outname = name;
        flags = 0;
        if ((tp = find_trans(name)) != NULL) {
          flags = tp->flags;
          if (tp->outgi != NULL)
            outname = tp->outgi;
        }
        if ((flags & EL_OMIT) == 0) {
          int i;
          
          printf("<%s", outname);
          for (i = 0; i < attrc; ++i) {
            printf(" %s=\"%s\"", attrn[i], attrv[i]);
          }
          if (flags & EL_EMPTY)
            putchar('/');
          putchar('>');
        }
        forget_attr();
        break;
      case '-':
        if (flags & EL_CDATA)
          cdata();
        else
          pcdata();
        break;
      case ')':
        store_name(name);
        outname = name;
        flags = 0;
        if ((tp = find_trans(name)) != NULL) {
          flags = tp->flags;
          if (tp->outgi != NULL)
            outname = tp->outgi;
        }
        if ((flags & EL_OMIT) == 0) {
          if ((flags & (EL_CDATA | EL_PCDATA)) == 0)
            putchar('\n');
          if ((flags & EL_EMPTY) == 0)
            printf("</%s>", outname);
        }
        flags = 0;
        break;
      case 'C':
      default: /* `&`, `D`, `N`, `E`, `I`, `S`, `T`,
                  `s`, `p`, `f`, `{`, `}`, `L`, `#`, `C`, `i`, `e` */
        ;
    }
  }
  putchar('\n');
}


int main(int argc, char *argv)
{
  qsort(xmltab, NELEM, sizeof xmltab[0], cmp);
  translate(xmltab);
  return 0;
}
