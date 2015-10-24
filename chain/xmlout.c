/* xmlout.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esisio.h>

#define NELEM 19

#define EL_EMPTY   0001
#define EL_CDATA   0002
#define EL_PCDATA  0004
#define EL_OMIT    0010

#define ATTR_MAX    128
#define BUF_SIZE   4096
#define NAME_SIZE   128

#define RE_CHAR '\xA'
#define RS_CHAR '\0'

struct trans {
  const char *ingi;
  const char *outgi;
  unsigned flags;
};

ESIS_Writer writer;

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
  
  while ((ch = getchar()) != EOF)
    if (ch == '\n')
      break;

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

size_t cdata(char *buf)
{
  int ch;
  char *p = buf;
  
  while ((ch = decode()) != EOF)
    if (ch != RS_CHAR)
      *p++ = ch;
  return p - buf;
}

void store_attr(char *buf, char **name, char **val)
{
  int ch;

  *name = buf;
  while ((ch = getchar()) != EOF) {
    if (ch == ' ' || ch == '\n')
      break;
    *buf++ = ch;
  }   
  *buf++ = '\0';

  *val = buf;
  if (ch == ' ') {
    while ((ch = getchar()) != EOF) {
      if (ch == ' ' || ch == '\n')
        break;
    }
    if (ch == ' ') {
      *val = buf;
      while ((ch = getchar()) != EOF) {
        if (ch == '\n')
          break;
        *buf++ = ch;
      }
    }
  }
  *buf++ = '\0';
}

void store_name(char *buf, char **name)
{
  int ch;
  
  *name = buf;
  
  while ((ch = getchar()) != EOF) {
    if (ch == '\n')
      break;
    *buf++ = ch;
  }
  *buf = '\0';
}

void translate(const struct trans tab[])
{
  int ch;
  char buf[BUF_SIZE], *name, *val;
  const char *outname;
  const struct trans *tp;
  unsigned flags;
  size_t len;
  int havepi = 0;
  
  while ((ch = getchar()) != EOF) {
    switch (ch) {
      case '?':
        trans_pi();
        havepi = 1;
        break;
        
      case 'A':
        store_attr(buf, &name, &val);
        ESIS_Attr(writer, name, val, ESIS_NTS);
        break;
        
      case '(':
        store_name(buf, &name);
        outname = name;
        if ((tp = find_trans(name)) != NULL) {
          flags = tp->flags;
          if (tp->outgi != NULL)
            outname = tp->outgi;
        }
        if (flags & EL_EMPTY)
          ESIS_Empty(writer, name, NULL);
        else
          ESIS_Start(writer, name, NULL);
        break;
        
      case '-':
        len = cdata(buf);
        if (flags & EL_CDATA)
          ESIS_Cdata(writer, buf, len);
        else
          ESIS_PCdata(writer, buf, len);
        break;
      case ')':
        store_name(buf, &name);
        outname = name;
        flags = 0;
        if ((tp = find_trans(name)) != NULL) {
          flags = tp->flags;
          if (tp->outgi != NULL)
            outname = tp->outgi;
        }
        if ((flags & EL_OMIT) == 0) {
          if ((flags & EL_EMPTY) == 0)
            ESIS_End(writer, outname);
        }
        break;
      case 'C':
      default: /* `&`, `D`, `N`, `E`, `I`, `S`, `T`,
                  `s`, `p`, `f`, `{`, `}`, `L`, `#`, `C`, `i`, `e` */
        ;
    }
    if (!havepi) puts("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    havepi = 1;
  }
}


int main(int argc, char *argv)
{
  unsigned opts = ESIS_CANONICAL;
  qsort(xmltab, NELEM, sizeof xmltab[0], cmp);
  writer = ESIS_XmlWriterCreate(stdout, opts);
  translate(xmltab);
  ESIS_WriterFree(writer);
  return 0;
}
