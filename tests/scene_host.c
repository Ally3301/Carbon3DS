#include "scene_loader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=2) return 1;
    SceneSection *s=scene_load(argv[1]);
    if(!s) return 2;
    assert(s->index_count%3==0);
    scene_free(s);
    /* Repeated ownership exercise for sanitizer leak detection. */
    for(int i=0;i<8;i++) { s=scene_load(argv[1]);assert(s);scene_free(s); }
    scene_free(NULL);
    return 0;
}
