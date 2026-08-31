/* ★Zenmai の殻を通すためだけの穴埋め。Higgins は Zork の本文データを持たない。
 *
 * `render.h` が `content.h` を include し、`render.c` の `draw_logical()` が
 * Zenmai の本文プール（`pool[]` / `citems[]`）を参照している。Higgins はこの関数を
 * **呼ばない**が、それだけではリンクが通らないので、型と空の定義を置く
 * （定義は content_stub.c）。
 *
 * ★**本筋は上流（Zenmai）で `render.c` から `draw_logical()` を切り出すこと。**
 *   今回は Zenmai を触らない約束なので、ここに印を残して先へ進む。
 *   切り出されたら、このファイルと content_stub.c は消える。
 */
#ifndef CONTENT_H
#define CONTENT_H

typedef struct { unsigned char blen, rlen; unsigned short boff, roff; } Item;
typedef struct { unsigned short off, cnt; } Line;   /* cnt == 0 は空行 */

extern const unsigned short pool[];
extern const Item citems[];

#endif
