#ifdef CMARK_DLL /* Only if building cmark lib as a DLL */
#define CMARK_EXPORT __declspec(dllexport)
#else
#define CMARK_EXPORT
#endif
