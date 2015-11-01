#if 0 /* Only if building a DLL */
#define CMARK_EXPORT __declspec(dllexport)
#else
#define CMARK_EXPORT
#endif
