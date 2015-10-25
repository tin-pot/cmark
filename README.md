About this Repository
=====================

Here is an overview of what kind of stuff this repository contains, in
addition to the original `cmark` where it came from.


The _CommonMark_ reference implementation
-----------------------------------------

The core of the sources here is the [_CommonMark_][cm] [reference
implementation][cmark] in the `src/` directory. These sources are used
to build two targets:

 1. The library `libcmark`, containing the parser itself, and

 2. the application `cmark`, a command-line processor to transform plain
    text in _CommonMark_ mark-up into HTML, XHTML, etc.

Other than the original `cmark` implementation (which uses `cmake`),
this one is wholly built through _Visual Studio 2008_ project files.
There will certainly come the day where I want to build this on Linux
(or rather: FreeBSD), and where I'll take a look at `cmake` again, but
don't hold your breath.

Only negligible changes were made to the [original sources][jgm]:

  - The command-line tool displays the _Git_ version identifier and
    source repository URL;

  - the ubiquitous `options` argument is not declared to be `int`, but
    a typedef'd `cmark_option_t`, which defaults to `int`. This is a nod
    to a future where there may be more than 32 options to pass around.

  - the element type name `html` from the [_CommonMark_ DTD][cmdtd] is
    renamed to `block_html`, unless this prevented with a feature macro.

That's it, as far as I can remember (I'd have to check the commit
history to be *really* sure).


An HTML document generator
--------------------------

Because the `cmark` tool does only output HTML/XHTML *fragments* (ie
sequences of HTML/XHTML elements), it is not readily usable to create
"free-standing" HTML documents from plain text input.

This can be done using the `cm2html` tool, which generates HTML
documents (hopefully) conforming to [ISO/IEC 15445:2000][iso-html]
(which is to W3C HTML what is [ISO 19005:2005-1 PDF/A][iso-pdfa] to
Adobe PDF, so to say).

It inherits most of `cmark`'s options, and adds

 1. `-t` or `--title` to set the document title;

 2. `-c` or `--css` to include a link to a given CSS file/URL in the
    document header.

Another modest extension is the support of "_pandoc_-style" document
headers: if the input text starts with (up to three) lines each with a
"`%`" character in column one, then these lines are not rendered in the
output, but taken to specify meta-information, in the order:

    % Document Title
    % Author Name
    % Date

If entered in this way, this meta-information will be placed in (up to
three) `<META>` elements in the document header, in a form according to
the [_Dublin Core_][dc-html] media meta-information standard (as I hope).


Using _CommonMark_ in an XML/SGML tool chain
============================================

The most recent (still in flux) and certainly the most important part of
this repostiory is concerned with "components" to make _CommonMark_ in
an "XML/SGML tool chain"---an idea that maybe needs explaining.

Purpose
-------

The "traditional" model, implemented by `cmark` and `cm2html` works well
in scenarios where it applies: plain-text in, out comes a formatted and
transformed representation of the input.

It does not work so well in use cases like these:

  - What if I want to *combine* a _CommonMark_ processor with tools
    for additonal syntaxes (in other words: how can one *extend* the
    _CommonMark_ syntax without too much effort every time one does so)?

  - What if I want to process not *whole* plain text documents as input,
    but only *fragments* of _CommonMark_ marked-up text, embedded in
    other documents (like source code, or HTML text);

  - Or more specifically: what if I want not only to *generate*, say,
    [_DocBook_][db] or [_LinuxDoc_][ld] documents from plain text files,
    but *process* such files too? The _LinuxDoc_ source file could
    already exist, and I want to rewrite or add a section, for which I'd
    prefer to use _CommonMark_ syntax (and not write the SGML mark-up
    myself)?

If you think about it, all these cases have in common the notion of not
processing "pure" *plain text*, but plain text embedded in "structured
documents": the difference between input and output is just that these
plain text fragments got replaced by new "structured content".

This is logically *exactly* the same model as thinking about an abstract
syntax tree, ie a hierarchy of nodes representing a document structure,
which get processed and transformed by a host of (independant) processes
rsp processors.


Concept
-------

Now the concept, standards, tools and technology to represent,
process and transform this kind of "structured documents" (concrete
representations of abstract syntax trees) has been around for a
few decades: SGML ([ISO 8879:1986][iso-8879]) and XML ([W3C since
1998][w3c-xml]) are really well-entrenched solutions for this kind of
applications.

The concept behind the implementation here follows more or less
completely form this perspective:

  - Process documents with tools follwing the "[UN*X philosophy][unix]":
    each should do only one job, and they should be easy to compose to
    flexibly do different jobs;

  - the most obvious and simplest way to compose these tools is to
    put them into a sequential [_pipeline_][pl] (again, as the UN*X
    concept, but available of course on pretty much every OS today), or
    equivalently, but more cumbersome, use a tool like _make_ to drive
    more complex processes.

  - The information flow from processor to processor must at each stage
    represent the *content* of a well-formed "structured document" in a
    well-defined and "faithful" way, but

  - the format (or representation) of this content at the interfaces
    between the tools can be choosen freely,

  - because we certainly do not want to include an XML parser in each
    tool---let alone an SGML parser,

  - but we must be able to transform from "external" formats like
    XML, SGML, or just good old plain text, *into* the internal
    representation, and at the end of the pipeline back again *from*
    the internal representation into (the same or another) "external"
    format,

  - where XML/SGML should be one of the options at both ends.

The meaning of "content" and "faithful" representation of it formalized
by the standard [_Element Structure Information Set_][esis] (ESIS) for
SGML documents, and by the [_XML InfoSet_][xml-is] for XML documents.


Internal Interface Format
-------------------------

The concept so far leaves open the choice of format for the input and
output of each tool (except for the very first and last one, which will
be transformations from and to "external" formats).

In my opinion the best and obvious choice is the [output format][format]
of James Clark's SGML and XML parsers (like `nsgml`): it is a text
format, it is very simple, it can be parsed and generated efficiently,
and as a nice side-effect we gain the ability to feed *any* XML or SGML
documents into our process chain by simply passing it through a parser
which is freely available and well-proven. And by definition (and trust
in James Clark ;-), this format *does* represent the *real* content of
a document, while disregarding questions of specific mark-up, which is
precisely the job a parser does.


Infrastructure
--------------

To ease the process of adapting tools like `cmark` so that they can
read and write the internal ESIS representation, functions to do so are
collected in a separate library called `libesisio`.

The input (parsing) side is handled by a call-back mechanism, and is
intentionally very similar in style and function to the API of the
_Expat XML Parser_ library (again written---originally--by James Clark,
and distributed under the liberal MIT license).

The output (generating) side is just a procedural interface, but
abstacts from the output format which is actually generated: an
application uses the same function calls to produce output in the
internal ESIS represenatation as it does when actually generating XML
output.


Processing Tools
----------------

Right now there is only one "real" processing tool:

  - `cmark_filter`

This is the `cmark` implementation wrapped into interfacing code so
that it does consume and generate text streams in the internal ESIS
representation format, using the element types of the _CommonMark_ DTD
(which `cmark -t xml` does in "regular" XML).

The other tools transform from and to formats in the "external world":

  - `txtin` is the simplest of all: it reads plain text and "wraps" it
    into the internal format (by placing it as `CDATA` content in a
    dedicated "wrapping" or "transport" element, called `<mark-up>`).

    It's source consists of a single file `txtin.c`, containing *37*
    lines of code, including blank lines.

  - `xmlin` is more "advanced": it uses the [_Expat 1.2_][expat] library
    (included in the `expat/` directory) to parse the input XML document
    (verifying thereby that it is well-formed), and outputs it in the
    internal ESIS representation.

    This more complex tool consists again of a single source file,
    `xmlin.c`, containing this time *56* lines of code---that's what
    libraries are made for.

  - `xmlout` is finally the "end of line" tool, transforming the
    internal format back into XML/SGML/XHTML/HTML. Transforming the
    element set of _CommonMark_ into (X)HTML does take some doing,
    therefore the single source file `xmlout.c` currently contains a
    whopping *309* lines of code.

    Apart from renaming/omitting/mapping _CommonMark_ elements it is
    also responsible for "entity-encoding" content (ie replacing "`<`"
    by "`&lt;`" etc, and handling the difference between eg `<br />`
    (XML) and `<BR>` (HTML).


Future Work
===========

While the available tools seem to work---remember that they are brand
new and barely tested---they are certainly not complete both regarding
features, as well as for example robustness. The all need better error
checking and reporting (though the infrastructure for it is in place in
`libesisio`) for example.

Concerning features and tools, I'd like to have:

 1. More comprehensive and flexible choices of output transformations:
    the `xmlout` tool manages to do it's job, and the resulting XML and
    HTML is---according to the little testing done so far---in fact
    well-formed XML and valid HTML (tested by feeding it to `xmlwf` and
    `nsgmls`), all you can do with it is chossing between four options
    (like the `-t` option of `cmark`).

    I'd like to see a *scriptable* transformation at this process stage,
    which would be the perfect place to generate a _table of contents_,
    or an _index_ for a document. And this again would be a perfect job
    for a scripting language.

    If it comes to implementing this, I'd probably opt for the
    _squirrel_ language and interpreter, mostly for it's style, and
    certainly for the interpreter's compactness. (A more exotic
    language, but an even smaller implementation, would provide a core
    _Scheme_ implementation like _tinyscheme_.)

 2. Of course more _syntax_ or mark-up processors are needed, that't one
    of the purposes of the whole endeavour after all.

    For me personally, this means that the next processor to fit into
    the tool chain will by an adaptation of my _Z Notation_ mark-up
    processor.

    From the experience so far, adapting a given tool to read and write
    the internal ESIS representation is not that much effort, but it
    obviously depends on the design and quality of the given tool's
    source code.

 3. More choices and flexibility on the input transformation side:
    while XML and SGML (HTML) covers many contents, and the input of
    "traditional" plain text _CommonMark_ sources is already covered by
    `txtin`, I'd like to have a scriptable tool here, too.

 4. More *specific* output formats, like _L<sub>A</sub>T<sub>E</sub>X_.
    Or---like `cmark`---plain text in _CommonMark_ syntax, maybe with
    prettier formatting (I have a plain-text formatter already to use as
    a base for this).

I'm sure you'll get your own ideas and preferences from experimenting
with this concept.

[cm]:http://spec.commonmark.org/0.22/

[cmdtd]:https://raw.githubusercontent.com/jgm/CommonMark/master/CommonMark.dtd

[jgm]:https://github.com/jgm/cmark

[cmark]:https://github.com/jgm/CommonMark

[iso-html]:https://www.cs.tcd.ie/misc/15445/15445.html

[iso-htmlug]:https://www.cs.tcd.ie/misc/15445/UG.html

[iso-pdfa]:https://en.wikipedia.org/wiki/PDF/A

[db]:http://www.docbook.org/

[ld]:http://soc.if.usp.br/manual/linuxdoc-tools/html/guide-1.html

[esis]:http://xml.coverpages.org/WG8-n931a.html

[xml-is]:http://xml.coverpages.org/WG8-n931a.html

[xml-is2]:http://www.w3.org/TR/xml-infoset

[iso-8879]:https://en.wikipedia.org/wiki/Standard_Generalized_Markup_Language

[dc-html]:http://dublincore.org/documents/dcq-html/

[w3c-xml]:http://www.w3.org/TR/xml/

[w3c-xhtml]:http://www.w3.org/TR/xhtml1/

[unix]:https://en.wikipedia.org/wiki/Unix_philosophy

[pl]:https://en.wikipedia.org/wiki/Pipeline_%28computing%29

[make]:https://en.wikipedia.org/wiki/Make_%28software%29

[format]:http://www.jclark.com/sp/sgmlsout.htm

[xml-can1]:http://www.w3.org/TR/xml-c14n

[xml-can2]:http://www.w3.org/TR/xml-exc-c14n/

[expat]:http://www.jclark.com/xml/expat.html

