% About the picol Directory
% mh@tin-pot.net
% 2015-11-02

# About the `picol/` directory #

To implement a _"scripted document transformation"_ that can be used in
the mark-up chain for example to rename tags, set attributes, insert
section numbers etc, I was looking for a script interpreter which would
be as small and simple as the job at hand is.

Because I know _[Tcl][]_ pretty well, which is both a simple scripting
language (at it's core &hellip;) and provides good string handling
capabilities, the _Tcl_ language was on the list of candidates right
from the start (among others such as [Squirrel][], or [Scheme][] in the
incarnation of _[tinyscheme][]_).

Apart from the current [_Tcl_ 8.6][tcl86], there are several
"[small][SmallTcl]" variants of _Tcl_, which share the concept, but
not the (current) _Tcl_ source code. Among them are:

  - _[Jim][]_: developed from scratch and actively developed;

  - _[Tiny Tcl 6.8][tinytcl]_: primarily by [Karl Lehenbauer][kl], and
    targeted at embedded systems;

  - _[TH1][]_: A Tcl-like interpreter used in the _[fossil][]_ version
    control system, written by [Richard Hipp][rh] (of _[sqlite][]_
    fame).

  - _[picol][]_: Is not small, but **tiny** (around 500 LOC)---written
    originally by [Salvatore Sanfilippo][ss] (who is also a co-author of
    _[Jim][]_, and then extended by [Richard Suchenwirth][rs].

I have collected two implementations of the _picol_ interpreter here:

 1. The 500-LOC tiny source `picol0.c` (renamed from `picol.c`):
    implementation written by Salvatore Sanfilippo.

 2. The version extended by [Richard Suchenwirth][rs]: the sources are
    distributed as [`picol0-1-22.zip`][p0-1-22], and the `picol.h`,
    `picol.c`, `*.pcl`, as well as the `tclIndex` file are from this
    archive.

I also "polished" the introductory texts accompanying the sources (the
`README.txt` written by Richard Suchenwirth, and the [blog entry][blog]
about _picol_ writen by Salvatore Sanfilippo) and placed them here as
`README_*.html` files.

As of now (2015-11-02) I'm not sure yet which one (if any) of these two
variants will be sufficient for my purposes---but getting away with
a 500-LOC interpreter to transform XML streams is certainly worth a
try &hellip;

And in the case that *both* _picol_ variants turn out to be too
minimalistic and restrictive, there's still _[TinyTcl][]_ to fill the
gap between _picol_ and a full-fledged, current _[Tcl 8.6][tcl86]_.


[Tcl]:https://en.wikipedia.org/wiki/Tcl
[Scheme]:https://en.wikipedia.org/wiki/Scheme_%28programming_language%29
[Squirrel]:http://squirrel-lang.org
[tinyscheme]:http://tinyscheme.sourceforge.net
[SmallTcl]:http://wiki.tcl.tk/1363
[jim]:https://github.com/antirez/Jim
[TinyTcl]:http://tinytcl.sourceforge.net
[kl]:http://wiki.tcl.tk/83
[fossil]:http://fossil-scm.org
[rh]:https://en.wikipedia.org/wiki/D._Richard_Hipp
[sqlite]:https://www.sqlite.org
[TH1]:http://fossil-scm.org/index.html/doc/trunk/www/th1.md
[picol]:http://wiki.tcl.tk/17893
[ss]:http://wiki.tcl.tk/9497
[rs]:http://wiki.tcl.tk/1683
[rs1]:http://wiki.tcl.tk/17893
[p0-1-22]:http://wiki.tcl.tk/_repo/wiki_images/picol0-1-22.zip
[blog]:http://oldblog.antirez.com/post/picol.html
[tcl86]:http://www.activestate.com/activetcl/activetcl-8-6
