# Leitura inicial do executável PSP decriptado

## Escopo

Esta nota descreve apenas o executável de `Need for Speed Carbon: Own the City` fornecido localmente. O binário decriptado foi mantido em `/tmp`; nenhum executável ou conteúdo proprietário foi copiado para o repositório. A análise de `CarReflectionState` fica deliberadamente fora deste documento, pois está em investigação separada.

## Resultado da decriptação

| campo | valor |
| --- | --- |
| entrada | `psp_game/sysdir/eboot.bin` |
| formato de entrada | PSP PRX protegido (`~PSP`) |
| saída temporária | `/tmp/nfs_carbon_otc_usa_eboot.dec` |
| formato de saída | ELF32 little-endian, MIPS R3000 / MIPS-II, EABI32 |
| módulo | `nfsGame` |
| hash SHA-256 da saída | `10515d5d2878816521bed26f4edfd8ad696e783bb21ecba7f10d16f6ba5ba40f` |
| `.text` | 0x38F9B4 bytes |
| `.rodata` | 0x30428 bytes |
| `.data` | 0x527EC bytes |
| `.bss` | 0x5E184 bytes |

O ELF está sem símbolos de funções úteis: a tabela de símbolos foi removida. Ainda assim, os nomes de classes, pools, mensagens e caminhos de recursos permanecem em `.rodata`. Ele também preserva as tabelas de relocalização específicas do PSP, necessárias para recuperar referências absolutas depois do carregamento do módulo.

A desmontagem via Capstone confirmou instruções MIPS válidas no ponto de entrada (`0x00000D34`), incluindo prólogo de pilha, `jal` para stubs do PSP e tratamento de estado inicial. Portanto, este arquivo é uma boa base para uma análise estática navegável; não é necessário tentar interpretar o PRX criptografado diretamente.

## Subsystems confirmados

### Streaming de pista e mundo

As referências abaixo aparecem agrupadas na mesma região de dados e descrevem uma cadeia concreta de carregamento:

- `tracks\\%d.viv`, `tracks\\WORLD_%d.viv` e variantes com barra inicial;
- `roadnetwork.bin`, `worldobjects.bin`, `visiblesections.bin`, `gonkulator.bin`, `smokeables.bin`, `TrackParticleEmitters.bin`, `trigulator.bin`;
- `Track Data Manager`, `Track Streaming Sections`, `TrackSharedMemoryTablePool`, `TrackSectionMemoryTablePool` e `TrackDataMainMemory`;
- `World`, `WorldEntity`, `WorldModel`, `WorldObject`, `WorldObstacle` e `WorldSceneryObstacleList`.

Isso confirma que `roadnetwork.bin` não é um artefato isolado usado só pelo minimapa: ele pertence ao sistema de mundo e está ao lado das tabelas que dividem a pista em seções visíveis, objetos e emissores. Para o port, a organização correta é tratar a rede como dado de navegação persistente e usar `visiblesections.bin`/seções de pista para decidir carregamento e descarregamento visual.

### Navegação e tráfego

O binário também contém `WRoadNetwork`, `WRoadNav Path Buffer`, `PathFinder`, `AStarNodeSlotPool`, `AStarSearchSlotPool`, `AStarHashSlotPool`, `TrafficTeleporter`, `AvoidableSlotPool` e `CookieTrailSlotPool`.

A evidência aponta para uma arquitetura de navegação em grafo: `WRoadNetwork` fornece a malha, `WRoadNav` armazena caminhos e os pools de A* evitam alocação durante a simulação. Isso é a referência certa para uma futura camada de tráfego no 3DS. O minimapa deve continuar sendo uma projeção de consulta da mesma rede, sem alterar ou simplificar o grafo usado por tráfego.

### Renderização e HUD

O módulo importa `sceGe_user`, a API da Graphics Engine do PSP, e `sceDisplay`; logo a versão original emite display lists diretamente para a GE. Há objetos e mensagens de `RenderManager`, `DisplayList`, `CachedDisplayList`, `DisplayListOptimizer`, `EAGL::ViewPort`, `HudManager`, `HudDrawGroup`, `HudElement`, `HudModel` e `HudPolygonPrimGroup`.

Também há cenas separadas chamadas `3D Scene World` e `3D Hud Scene`, além de `Could not create 3D HUD scene` e `Could not create 3D HUD viewport`. Isso confirma que HUD e mundo eram passes distintos. No port, esse é o limite de estado que deve ser preservado: o TEV/alpha de materiais do mundo não pode vazar para sprites do HUD, e o HUD deve restaurar explicitamente a textura, a combinação TEV, o blend e o depth state.

### Céu e iluminação de mundo

O executável contém `SKYDOMESUNSET01`, `SKYDOMESUNSET06`, `RB_SKYDMESUNSET`, `RG_SKYDMESUNSET` e `RJ_SKYDMESUNSET`, além de `XEM_ROADSKYELW`. Isso confirma que o PSP tinha famílias de skydomes, em vez de depender apenas do clear color/fog.

A ação segura para o port é localizar essas malhas/texturas nos arquivos de pista e carregá-las como passe de fundo dedicado: depth write desligado, depth test sempre/LEQUAL conforme o pipeline atual, culling ajustado para a face interna e estado TEV restaurado antes de mundo/HUD. Não deve reutilizar o compositor de HUD para compor o céu.

## Consequências práticas para o port

1. **Streaming:** manter um cache de seções físicas/visuais separado do grafo `roadnetwork`. A rede deve permanecer disponível para busca de caminho mesmo quando a geometria de uma seção está descarregada.
2. **Minimapa:** a projeção deve se basear no nó/aresta mais próxima e no trecho conectado ativo, não apenas na coordenada mundial. Para passagens, pontes e ruas paralelas, um custo de continuidade a partir da aresta anterior evita saltos visuais.
3. **HUD:** concentrar a restauração de GPU em uma função de início do HUD. Ela deve configurar explicitamente blend, depth, viewport, textura e TEV, sem assumir o estado do draw anterior.
4. **Skybox:** implementar como passe do mundo isolado e testar primeiro com uma única malha/texture original. Só depois adicionar gradiente, névoa e iluminação ambiente.
5. **Carros:** os nomes `NFSCar_TextureShiny`, `GAME::CarReflectionTexture` e `GAME::CarReflectionState` confirmam que o caminho existe no PSP, mas não definem sua fórmula. A reimplementação deve esperar a documentação específica da rotina MIPS para não confundir material de pintura com textura comum do mapa.

## Próxima etapa de engenharia reversa

Para avançar de nomes para funções sem depender de adivinhação:

1. carregar o ELF decriptado em Ghidra com `MIPS:LE:32:default` e imagem-base PSP `0x08800000`;
2. aplicar as relocalizações PSP antes de seguir referências a strings;
3. renomear os primeiros blocos por ancoragem de strings: `Track Data Manager`, `WRoadNetwork`, `Track Streaming Sections`, `HudManager` e `RenderManager`;
4. reconstruir chamadas entre o carregador de `roadnetwork.bin`, o carregador de seções e `WRoadNav`;
5. comparar os campos que a rotina lê com o parser já existente no port antes de traduzir qualquer algoritmo.

