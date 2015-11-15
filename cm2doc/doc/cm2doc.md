% cm2doc – CommonMark Document Generator
% mh@tin-pot.net
% 2015-11-14
% lang: en
% X-email: mh@tin-pot.net
% X-doc.id: cm2doc
% X-doc.ed: Working Draft

0   Introduction
----------------

1   Synopsis
------------

2   Replacement file syntax
---------------------------

  - "`SYNTACTIC LITERAL`"
  - _**delimiter role**_
  - _**TERMINAL VARIABLE**_
  - _**Terminal Constant**_

### 2.1   ASP Backend ###

<DIV class="hi">

(1) *repl file* = { *rule* | *comment* } ;

(2) *comment* = _**eol com**_, { _**CHAR**_ } , _**EOL**_ ;

(3) *rule* = *start tag rule* | *end tag rule* ;

\[01\] *start tag rule* = *start tag* , { S } , [ _**bol**_ , { S } ] ,
                                  *repl text* , [ _**bol**_ , { S } ] ;

\[01\] *end tag rule* = *end tag* , { S } , [ _**bol**_ , { S } ] ,
                              *repl text* , [ _**bol**_ , { S } ] ;

\[01\] *start tag* = _**stago**_ , *name* , _**tagc**_ ;

\[01\] *start tag* = _**etago**_ , *name* , _**tagc**_ ;

\[01\] *repl text* = { *string* , { S } } ;

\[01\] *string* = _**lit**_ , { *string char* - ( _**lit**_ ) 
                | *escape* | *attr subst* } , _**lit**_  ;

\[01\] *string* = _**lita**_ , { *string char* - ( _**lita**_ ) 
                | *escape* | *attr subst* } , _**lita**_  ;

\[01\] *string char* = _**CHAR**_ - ( _**esc**_ | _**attsubo**_ ) ;

\[01\] *escape* = _**esc**_ , ( _**esc**_  | _**attsubo**_
               | _**lit**_ | _**lita**_
               | &quot;`n`&quot; | &quot;`t`&quot;  | &quot;`r`&quot;
               | &quot;`s`&quot;  | &quot;`f`&quot; | *octal number* ) ;

\[01\] *octal number* = _**Oct Digit**_ , { _**Oct Digit**_ } ;

\[01\] _**Oct Digit**_ = &quot;`0`&quot; | &quot;`1`&quot;
                       | &quot;`2`&quot; | &quot;`3`&quot;
                       | &quot;`4`&quot; | &quot;`5`&quot;
                       | &quot;`6`&quot; | &quot;`7`&quot; ;

\[01\] *attr subst* = _**attsubo**_ , *name* , _**attsubc**_ ;

\[01\] *name* = _**NMSTART**_ , { _**NMCHAR**_ } ; (* XML/SGML name *) 

\[01\] 

</DIV>

<DIV class="hi">

\[01\] _**stago**_ = "`<`" ;

\[01\] _**etago**_ =  "`</`" ;

\[01\] _**tagc**_ = "`>`" ;

\[01\] _**attsubo**_ = "`[`" ;

\[01\] _**attsubc**_ = "`]`" ;

\[01\] _**lit**_ = '`"`' ;

\[01\] _**lita**_ = "`'`" ;

\[01\] _**NMSTART**_ = _**UCNMSTRT**_ | _**LCNMSTRT**_ ;
                                 (* Or XML _**name start character**_ *)

\[01\] _**NMCHAR**_ = _**NMSTART**_ | _**Digit**_ 
                    | _**UCNMCHAR**_ | _**LCNMCHAR**_ ;
                                       (* Or XML _**name character**_ *)

\[01\] _**NMCHAR**_ = _**NMSTART**_ | _**Digit**_ 
                    | _**UCNMCHAR**_ | _**LCNMCHAR**_ ;
                                       (* Or XML _**name character**_ *)

\[01\] _**NMCHAR**_ = _**NMSTART**_ | _**Digit**_ 
                    | _**UCNMCHAR**_ | _**LCNMCHAR**_ ; 
                                       (* Or XML _**name character**_ *)

\[01\] _**CHAR**_ = ? Any XML character ? ;

\[01\] _**EOL**_ = ? Line break ? ;

</DIV>

### 2.2   ASP Backend Example ###

A very simple example is given in the _ASP Backend_ description:

~~~~
<memo>         ".MS"
<sender>       "From: "
<forename>     " "
<receivers>    "To: "
<contents>     ".PP"
</memo>        ".ME"
~~~~

A more relevant example uses _attribute substitution_ to convert
a _CommonMark_ node into a HTML heading:

    <CM.HDR>      + "<H[level]>"
    </CM.HDR>     "</H[level]>" +

This appends the value of the _CommonMark_ attribute `level` to the
`H` and thus generates the appropriate HTML element type name (good
that the `level` attribute *does* start counting at `level="1"`!).

But wait---an _end tag_ does not bear attributes, and consequently
the _ASP Backend_ does not substitute the `[level]` attribute
reference! Duh.

So we want (and *need* for our purpose) a slightly better variant
of the  _Replacement File_ syntax.

But before we start extending the syntax we use the opportunity
to "update" some minor design choices.


### 2.3   A Modest Update of the Replacement Syntax ###

It shows that the syntax for the _ASP Backend_ was introduced
a long time ago: we expect all our document types to be _Unicode_-based
(if not *encoded* in a UTF schema), so referencing characters by
*octal numbers* seems really outdated.

Furthermore, the "`\f`" escape sequence is not only a bit pointless
(like the "`\r`" for CR, or for my taste "`\t`" too), but borderline
dangerous: the FORM FEED control character it produces is **not** 
allowed in XML documents (nor in HTML, nor in SGML's _Reference Syntax_).
The only "safe" control characters from the C0 set in our context are:

 1. U+0009 HT ("`\t`"),
 2. U+000A LF ("`\n`"),
 3. U+000D CR ("`\r`"),

so we decide the "`\f`" must go.

Instead of *octal* numbers for _code points_ we want to use *hexadecimal
numbers*; which leads us to a new production for *escape*, where we
also specify that "`\`" in front of *any* character which is not a 
*hexadecimal digit* nor one of "`n`", "`r`", "`t`", "`s`" will just
"mask" this character: this is just a generalization of the 
previous rule which enumerated **esc** ("`\"`"), **lit** ("`\"`"), and
**attsubo** ("`\[`") as the *only* "maskable" characters. 

So the new version for the *escape* production is:

<DIV class="hi">

\[01\] *escape* = _**esc**_ , 
               ( *masked char* | *esc letter* | *hex number* , [ S ] ) ;

\[01\] *esc letter* = &quot;`n`&quot; | &quot;`t`&quot; 
                    | &quot;`r`&quot;  | &quot;`s`&quot; ;

\[01\] *masked char* = _**CHAR**_ - ( _**Hex Digit**_ |  *esc letter* ) ;

\[01\] *hex number* = _**Hex Digit**_ , 5 * [ _**Hex Digit**_ ] ) ;

\[01\] _**Hex Digit**_ = _**Digit**_ 
          | &quot;`A`&quot; | &quot;`B`&quot; | &quot;`C`&quot; 
	  | &quot;`D`&quot; | &quot;`E`&quot; | &quot;`F`&quot; 
	  | &quot;`a`&quot; | &quot;`b`&quot; | &quot;`c`&quot; 
	  | &quot;`d`&quot; | &quot;`e`&quot; | &quot;`f`&quot; ;

</DIV>

The *hex number* here specifies an _Unicode_ _code point_ (technically in the
range _0_ .. _10FFFF_, while not *all* of these _code points_ are allowed),
and can have one to six digits. Since there is no way to indicate an
earlier end to the sequence of _**Hex Digits**_, one can use a _**SPACE**_
for this purpose, which will then be "gobbled" up the the hex number:
that's the [ S ] after the *hex number* token in production \[00\].

\[**NOTE:** These conventions for the use of "`\`" are a direct
import from W3C CSS Level 2, and *intentionally* so. Any differences
are a remaining flaw in *this* specification. IMO it is a good choice
to *replicate* rather than *re-invent* a syntactic detail as mundane
as the "backslash-escape" sequences. \]

With this out of the way, we turn back to our showstopper keeping us
from translating the _end tag_ of the _CommonMark_ heading element type:

    </CM.HDR>     "</H[level]>" +

can **not** be used in the _ASP Backend_ (as documented), but it is
obvious what it should do, and there is no reason to "forbid" it.

\[**NOTE:** The reason that the _ASP Backend_ would not substitute
_attribute values_ in the replacement text for _end tags_ is probably
simple: the implementation must *remember* which attributes were
defined with which values in the corresponding _start tag_, and this
requires obviously more effort to implement than just using the
attributes "at hand", in the _start tag_. \]


### 2.2   Extension: Attribute Replacements for _end tags_ ###

There is no change needed in the grammar for this extension---on the
contrary, the _ASP Backend_ description had to state it *apart* from
the grammar (which kind of says otherwise):

> There is one exception for the `ATT_OPEN` token: `ATT_OPEN` is never
> recognised inside the replacement text of an *end_repl*, because there
> are no attributes associated with an endtag.

We simply drop this exception and nitpick that _attributes_ in SGML are
not "associated" with (either a *start* or *end*) *tag*, but with an
*element* (they just happen to be specified inside the *start tag*), so
this exception had no conceptual reason anyway.

\[**NOTE:** We have called the `ATT_OPEN` token _**attsubo**_ here, and
the non-terminal *end_repl* obviously is our *end tag rule*. \]

Now the *end tag rule*

    </CM.HDR>     "</H[level]>" +

produces a flawlessly matching `<H1>`, `<H2>`, ... tag for the *start tag*
of the same element.


### 2.3   Extension: Attribute presence and value selectors ###

Continuing to set up a _Replacement Definition_ file for HTML output,
we run into the next obstacle: there is only one _CommonMark_ node
type for lists, and this should be mapped to either an `<UL>` or an `<OL>`
(*start* and *end*) *tag* in the output---but this time our little
hack to *substitute an attribute value* into the output GI does not
work: this is the _attribute definition list declaration_ in the
_CommonMark_ DTD:

    <!ATTLIST list
              type        (bullet|ordered)   #REQUIRED
              start        CDATA             #IMPLIED
              tight       (true|false)       #REQUIRED
              delimiter   (period|paren)     #IMPLIED>

We want to produce `<UL>` tags if `type` has the value `bullet`, and
`<OL>` tags if it has the value `ordered`.

One could dream up elaborate expression inside the _attribute substitution_
elements in the replacement text, something like

    <CM.HDR>   "<[type==ordered?OL:UL]>" +

But this quickly leads to complications in parsing, nesting, recursion,
quoting and escaping and is just not worth the effort (_been there, done
that_).

A much simpler and cleaner approach is to---again---think of existing
practice, which means: think of CSS: what about some kind of "selector"
for alternative rules concerning the same source element type (rsp.
_CommonMark_ node type)?

We can easily see that the equivalent of a simple _attribute selector_
(we refer to the definition in _[W3C Selectors][w3c-sel-att]_, but
the same selector has been around in CSS forever):

What in CSS looks like this:

    CM.HDR[type=ordered] {
	/* Arrange for a nice display of an *ordered* list */
    }

    CM.HDR[type=bullet] {
	/* Define the style properties for an *unordered* list */
    }

could look and feel very naturally in our _Replacement Definition_
syntax too:

    <CM.HDR type="ordered">   + "<OL>"

    <CM.HDR type="bullet">    + "<UL>"

Very easy to understand and apply. However, the same required "selector"
look a bit wierd in a supposed-to-be *end tag* rule:

    </CM.HDR type="ordered">  "</OL>" +

    </CM.HDR type="bullet">   "</UL>" +

Let alone that we have to write *four* rules, repeating the _attribute value_
"selector"---shouldn't *two* rules be enough?

I do think so, and while an _attribute value_ selector in an *end tag rule*
is certainly straightforward and possible, I'd prefer to simply drop
the distinction between *start tag* and *end tag* rules alltogether, and
just have *one* kind of rule, which specifies *both* the replacement text
(if any) for the *start tag* and the replacement text (again, if any) for the 
*end tag* of the "selected" element. -- You can see how this moves into
the CSS direction even further &hellip;!

The only "design decision" here is 

  - how to separate the two replacement texts, and
  - how to indicate the *absence* of one or both replacement texts.

I chose to use a SOLIDUS "`/`" to separate the *start tag* replacement
text before the "`/`" from the *end tag* replacement text after it. This
would also suffice to indicate an *absent* replacement text for the
*end tag* (the "`/`" is missing too), an the *absence* of a replacement
text for the *start tag* (there is nothing in front of the "`/`").

However, I find it a more pleasing syntax if an absent replacement text
is *indicated* explicitly, and choose HYPHEN-MINUS "`-`" for this.

\[**NOTES:**

  - The choice of "`/`" was influenced by the allusion to the *end tag
    open* delimiter _**etago**_ ("`</`"), and

  - the "`-`" fits nicely as an "opposite" to the "`+`" which is *only*
    used together with a replacement text, but is *outside* of it. \]


### 2.4   Extended Grammar ###

With just the equivalents for the W3C selectors

 1. `E[attr=val]`: Select rule if element `E` has the attribute `attr`
    with the value `val`;

 2. `E[attr]`: Select rule if element `E` has an attribute `attr`, 
    regardless of the value of it.

we have an elegant solution to our HTML problem:

    <CM.HDR type="ordered">   + "<OL>" / "</OL>" +

    <CM.HDR type="bullet">    + "<UL>" / "</UL>" +

And we also have a solution for one problem which was not mentioned yet:
how to deal with _attribute substitutions_ where the attribute is not
defined (in the current element)? The _ASP Backend_ seems to terminate
with an error, but using alternative rules with selectors:

    <E>               % General case.
    <E attr>          % Select if `E` *has* an attribute `attr`.
    <E attr="1">      % Rule for specific value no. 1
    <E attr="2">      % Rule for specific value no. 2
    % and so on ...

we can fine-tune what to do in each case, and can always avoid
running into an "undefined attribute" in a replacement text.

Here is the syntax for the *element tags replacement rule*, which combines
the *start tag* and *end tag* rules from the past:


<DIV class="hi">

\[01\] *tag rule* = *tag* , [ *start-tag repl text* | _**absent**_ ] ,
                [ _**sep**_ , ( *end-tag repl text* | _**absent**_ ) ] ;

\[01\] *tag* = ( _**stago**_ | _**etag**_ ) , *name* ,
                         [ { *S* } - , { *attr spec* } ] ,  _**tagc**_ ;

\[01\] *attr spec* = *name* , { *S* } , _**vi**_ , { *S* } ,
                                          *attr val literal* , { *S* } ;

\[01\] *attr val literal* = _**lit**_ ,
                       { *attr val elem* - ( _**lit**_ ) } , _**lit**_ ;

\[01\] *attr val literal* = _**lita**_ ,
                     { *attr val elem* - ( _**lita**_ ) } , _**lita**_ ;

\[01\] *attr val elem* =  *attr val char* | *escape* ;

\[01\] *attr val char* = _**CHAR**_ - ( _**esc**_ ) ;

\[01\] _**absent**_ = &quot;`-`&quot; ;

\[01\] _**sep**_ = &quot;`/`&quot; ;

\[01\] _**vi**_ = &quot;`=`&quot; ;

\[01\] 

\[01\] 

</DIV>

\[**NOTE:** This production allows the forms 

 1. `<E> / "repl text"`   for an absent *start tag repl text*, and
 2. `<E> "repl text"`   for an absent *end tag repl text*.

as a shorter variant style, but I prefer the more explicit style

 1. `<E> - / "repl text"`   for an absent *start tag repl text*, and
 2. `<E> "repl text" / -`   for an absent *end tag repl text*.

Remember that it is not only the *replacement text*  which is absent,
but there will be **no trace** from the omitted tag in the output;
and this fact should be easy to glimpse from the replacement rules
in my opinion. \]


### 2.5   Extension: "Inheriting" Attribute Values ###

As soon as one can use _element attribute values_ to substitute
into the _replacement text_, ideas spring up *what else* could be
inserted there.

The YYYY-MM-DD date stamp below the title of this document is a
good example---this is **not** a date I typed in, but the date
when the HTML output was *generated* by `cm2doc`.

This "current date" value (named "`DC.date`") and other "pseudo-attributes"
are used to represent "meta-data" (in a loose sense, but as we'll see
in a *very strict* sense too) about the document.

Some of them are set up and initialized by the `cm2doc` program, but
all of them (and arbitrary other "pseudo"-attributes) can be set
in the first lines of the input _CommonMark_ text.


#### 2.5.1   An Extra Outer "Pseudo"-Element ####

"Pseudo"-Attributes like "`DC.date`" and other meta-data must be stored
*somewhere*, and should not be treated fundamentally different than all
the "real" attribute values belonging to the _CommonMark_ nodes. 

And technically they *are* ordinary attributes, but not of any real
_CommonMark_ node (or element, so to say), but there is an *extra*
"pseudo"-element *outside* the document tree, and the document root
element (in the _CommonMark_ case this is "`CM.DOC`" or "`document`"
in the _CommonMark_ XML DTD) is *technically* and *internally* not
the root element any more, but the (only) child of this "universe"
"pseudo"-element.

This outer "pseudo"-element can't be seen or adressed from the 
perspective of the _Replacement Definition_ (it does not even have a
name!)---but you can see it in the RAST output, lists it with the
"pseudo"-name "`#0`", and also provides the complete list of 
"pseudo"-attributes in it with their (no: not "pseudo") values.

But keep in mind that it is strictly an implementation decision to
have this "pseudo"-element for the sole purpose of storing "pseudo"-
attributes *outside* the source document's proper ESIS.

\[**NOTE:** Of course "`#0`" is no valid _generic identifier_ in the
sense of ISO 8879:1986/Cor.2:1999 (defining SGML) nor ISO 13673:2000
(defining RAST and the ESIS); and the outer "pseudo"-element is really
**not** part of the _CommonMark_ ESIS anyway---so the RAST output should
be mute about it unless specifically requested to output it too (in a
non-conforming mode!). \]


#### 2.5.2   Looking Outside the Document ESIS ####

In the _Replacement Definition_ syntax, an _attribute substitution_
refers (by name) to an attribute in the *current* element (ie the
element for which the rule was selected). As a consequence, there
is no way to access the attribute values (in a _replacement text_) of
*enclosing* elements, let alone the *all-enclosing* outer "pseudo"-element
where the meta-data live.

This is _per se_ perfectly justified insofar that the SGML/XML document
model has no concept of "inheriting" attributes from ancestor elements
to children.

What *does* have a similar concept is---again!---CSS, where it is called
"the cascade": for example, section "6.2 Inheritance" says:

> Some values are inherited by the children of an element in the
  document tree, as described above. Each property defines whether it is
  inherited or not.
>
> Suppose there is an H1 element with an emphasizing element (EM)
  inside:
> 
>     <H1>The headline <EM>is</EM> important!</H1>
> 
> If no color has been assigned to the EM element, the emphasized "is"
  will inherit the color of the parent element, so if H1 has the color
  blue, the EM element will likewise be in blue.
> 
> When inheritance occurs, elements inherit computed values. The
  computed value from the parent element becomes both the specified
  value and the computed value on the child.

But note carefully that CSS deals with **two different concepts** here:
what is "inherited" are CSS **properties**, and **not attribute** values
in the HTML (or XML) document tree.

Nevertheless, CSS was the inspiration for the final extension of the
_ASP Replacement File_ syntax discussed here:

You can "inherit" or rather "access" the _attribute values_ of _enclosing
elements_ by prefixing a FULL STOP "`.`" or a **Digit** from "`0`" to
"`9`" to the _attribute name_ in the _attribute substitution_:

While in a _replacement text_ the substitution

    ... [own-attr] ...

can only access an attribute (named "`own-attr`") defined in the current
element (the one for which the _tag replacement rule_ was selected),
the prefixed "name"

    ... [.own-attr] ..

will not only find the value of an "`own-attr`" in the *current* 
element, but in **any** element outside too (and use the value of
the first defined attribute encountered on the "way to outside").
And this will eventually find the "pseudo"-attributes in the outermost,
un-named, "pseudo"-element too.

So the simple rule is: prefix a "`.`" in front of the "pseudo"-attribute's
name to access meta-data like "`.CM.date`" from inside the _replacement text_
for *any* element tag.

\[**NOTE:** Because FULL STOP "`.`" and **Digit** are not in the
*name start character* character class (but they **are** in the *name
character* class), the result of prefixing one of these character to
an _attribute name_ is **not** a _name_ (but still a _name token_).
As a consequence, there is never an ambiguity whether an _attribute
substitution_ contains a regular _attribute name_, or an _attribute
name_ prefixed by "`.`" or **Digit**. -- This holds for the SGML
_Reference Concrete Syntax_ as well as for any HTML variant and for XML.
\]

You can prefix a "`.`" to *any* _attribute name_ and get the same 
effect: the _attribute value_ is looked up in the extended scope extending
right to the outermost "pseudo"-element. Thus you can access the
"`delim`" attribute in the _CommonMark_ `CMARK_NODE_LIST` node in
the _replacement text_ for the *list item start tag* `<CM.LI>`:

    <CM.LI> '<LI class="[.delim]">'

and "copy" it from the source _CommonMark_ node to the result HTML
`<LI>` element's "`class`" attribute, and then refer to this attribute
in the `<LI>` element in say a CSS (again!) syle sheet using a "class
selector":

    LI.paren  { ... }

    LI.period { ... }

So after all---in a sense, but a real sense---we **can** "inherit"
_element attributes_, and even *across* document types with the little
"attribute lookup scope hack" we have here.


#### 2.5.3   Predefined "Pseudo"-Attributes ####


Here is the list of currently provided "pseudo"-attributes, with
their default values and how this default can be overwritten:

  - `DC.title` : The document title. Default "`Untitled Document`".
     Can be set in the first "meta line" (see below), or using the
     command-line option argument `--title "My Title"` (alias `-t "My Title").

  - `DC.creator`: The document's author. Default: the value of the
     _environment variable_ `LOGNAME` or else `USERNAME`, or "`N.N.`" if
     both fail to provide a value. Can be set in the second "meta line".

  - `DC.date`: The local date in the ISO 8601:2004 format "YYYY-MM-DD",
     when `cm2doc` was run. Can be set in the third "meta line".

  - `CM.ver`: The version string of the _cmark_ reference implementation,
     which is used as the _CommonMark_ parser.

  - `CM.doc.v`: The version string of the `cm2doc` program.

  - `CM.css`: The URL for the output (HTML or XML) document's CSS (again!)
     style sheet. Default: "`default.css`". Can be set using the
     command-line option argument `--css my_style.css` (alias `-c my_style.css`).


#### 2.5.4   Document Meta-Data Lines ####

One can specify new "pseudo"-attributes as well as override any of
the pre-defined default values in the first few lines at the beginning
of (the first) _CommonMark_ input file presented to `cm2doc`.

The "syntax" (if it is worth the name) for this is inherited from _pandoc_,
where it is called a "title block":

If the _CommonMark_ input starts with lines having a PERCENT SIGN "`%`"
in the leftmost column, the content of the *first three* such lines
will be assigned to

 1. `DC.title`,
 2. `DC.creator`,
 3. `DC.date`

in other words: the first three such line specify the document's title,
author, and date (presumably the "last changed date").

\[**NOTE:** So far this is the same as in _pandoc_, with the exception
that the `DC.creator` line will **not** be parsed and split at 
SEMICOLON into *multiple* author names---for now at least. \]

If there are four or more such "metadata" lines, you must (beginning
in the 4th line) specify **both** the "pseudo"-attribute's name and
value. You do this by placing a ( COLON , SPACE ) right after the
attribute name:

    % The Title
    % A. U. Thor
    % 2015-11-14
    % DC.subject: example; mark-up language; plain text; Markdown
    % email: a.u.thor@example.org

\[**NOTE:** In case you wondered why the "author" metadata is called
`DC.creator` and not simply `author`---look up [_Dublin Core_][dc], look
at [this website][dc-html], then look at this document's [HTML `<HEAD>`
element][html-dc]. \]

[dc]:
[html-dc]:


3   Example: Generating HTML Output
-----------------------------------

4   Implementation notes
------------------------

5   Future work
---------------

A   Replacement Definition Syntax
-----------------------------------

B   Character Set and Encoding
-----------------------------------

C   ISO/IEC 13673:2000, ESIS, RAST and XML Information Set
----------------------------------------------------------

D   The _CommonMark_ document type (DTD) used in _cm2doc_
---------------------------------------------------------

[dc-html]:http://dublincore.org/documents/dc-html/
[cm-dtd]:https://raw.githubusercontent.com/jgm/CommonMark/master/CommonMark.dtd
[cmdoc-dtd]:
