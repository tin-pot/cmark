Sample \<xml> document

  - No reference to external DTD.
  - All elements declared in internal subset.

This should be easy!

Using CommonMark
================

We can use _CommonMark_ syntax *inside* a `<mark-up>` element,
but we must take care to avoid to accidentally use
\<XML> mark-up: eg, place the script inside a `CDATA`
section.

Here is a _fenced code block_ with an _info string_:

``` C
int main(void)
{
  printf("Hello world!\n");
  return 0;
}
```

Here is an example in Z Notation:

+-- Name ---
    n : %N
|--
    %E m : %N | m > n
---

That ends our little example.


