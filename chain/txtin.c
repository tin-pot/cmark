/* txtin.c */

#include <stdio.h>
#include <esisio.h>

ESIS_Writer writer;

#define LINELEN 32

void pump(void)
{
    char buffer[LINELEN];
    size_t nread;
    
    while ((nread = fread(buffer, 1, sizeof buffer, stdin)) > 0U)
      ESIS_Cdata(writer, buffer, nread);
}

int main(void)
{
    const char *atts[] = {
      "mode", "vertical",
      "notation", "CommonMark",
      "label", "plain-text input",
      NULL
    };
    
    writer = ESIS_WriterCreate(stdout, 0U);
    puts("?xml version=\"1.0\" encoding=\"UTF-8\"");
    ESIS_Start(writer, "mark-up", atts);
    pump();
    ESIS_End(writer, "mark-up");
    
    ESIS_WriterFree(writer);
    
    return 0;
}
