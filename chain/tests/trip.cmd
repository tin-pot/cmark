@set savepath=%PATH%
@path C:\Projects\cmark\VC9\Debug;%PATH%

txtin < sample-md.md > sample-md-in.int
xmlin < sample-xml.xml > sample-xml-in.int
nsgmls sample-sgml.sgml > sample-sgml-in.int

for %%f in (*-in.int) do (
	cmark_filter < "%%f"             > "%%~nf-cmark.int"
	xmlout -xml  < "%%~nf-cmark.int" > "%%~nf-cmark-out.xml"
	xmlwf          "%%~nf-cmark-out.xml"
)

@path %savepath%
