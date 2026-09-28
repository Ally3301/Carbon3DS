# Pesquisa de arquitetura PSP

## Estrutura observada

O executável MIPS fica em `sysdir/eboot.bin`. Os dados são separados por função:

- `usrdir/cars/loadingbe/allcars.viv` e `loadingfe/allcars.viv`: 43 arquivos de carro; `common.viv` contém geometria compartilhada de rodas.
- `usrdir/tracks/`: mundo aberto, pistas e variantes de corrida; `opwd_3000.viv` e `world_3000.viv` são os candidatos prioritários para streaming e mundo livre.
- `usrdir/global/`: recursos globais e bibliotecas compartilhadas.
- `usrdir/frontend/`: telas, componentes, transições e controles de UI.
- `usrdir/hud/`: HUD por idioma e pacotes de loading.
- `usrdir/effects/` e `eavis/`: partículas, pós-processo e arte de apresentação.

Os carros PSP mantêm o encadeamento RefPack -> ELF32/MIPS -> relocações EAGL. As relocações permitem descobrir a semântica de recursos mesmo sem executar o binário: renderer, textura/material TAR e estados como `GAME::CarReflectionState`.

## Evidência de renderização

A pesquisa PSP confirma que `_splay` é um fluxo de normais alternativo, não um LOD. Em 894 de 895 pares, topologia, posições e UVs são idênticas. A diferença é a direção de reflexão. Quase todos os pacotes visuais referenciam `CarReflectionState`; apenas os Gouraud não o fazem.

Isso separa claramente duas tarefas:

1. extrair a geometria e os materiais dos VIV/ELF;
2. recuperar a rotina que transforma `CarReflectionState` em coordenadas/reflexo e composição da pintura.

A primeira pode usar parsing estrutural. A segunda precisa de análise de execução MIPS.

## Método recomendado para MIPS

Não partir apenas de Ghidra e nomes de strings. Usar três fontes em conjunto:

1. **Call sites e xrefs**: localizar os símbolos EAGL, `CarReflectionState`, `NFSCar_TextureShiny`, `PAINT`, `WINDOW` e funções que os registram.
2. **Dados de runtime**: comparar descritores de materiais/relocações do PSP, os campos escritos em memória e os parâmetros passados a cada chamada.
3. **Emulação/instrumentação controlada**: registrar argumentos de funções candidatas, matrizes, texturas e estados antes/depois de renderizar um carro; repetir com Paint, Glass, Chrome e Rubber.

A rotina final deve ser descrita semanticamente antes de ser portada: entradas, transformações de coordenada, geração de UV/normal de reflexão, textura usada, regra de blend e classes de material.

## Pontos reutilizáveis antes do renderer de carros

- As quatro malhas de roda PSP de `common.viv`, com variantes `_splay`, já têm pacote/material claro.
- Os pacotes HUD e frontend podem ser usados para recuperar layout, fontes, estados de menu e transições; não devem ser passados pelo shader de mundo.
- Os VIV de tracks podem revelar uma política de carregamento por modo: mundo aberto versus pistas de corrida.
- `effects.msh`, `feeffects.msh` e `eavis/textures` são uma fonte melhor para partículas e overlays do que criar efeitos genéricos.
- As texturas PSP já decodificadas devem manter um status por asset: estruturalmente válido, visualmente validado ou pendente.

## Correspondência com o port

| PSP | Port atual | Próximo passo |
|---|---|---|
| allcars/common VIV | N3P1 + vehicle asset | migrar apenas após converter semântica de material |
| CarReflectionState + _splay | passes approximados de paint/window | substituir por dados e regra PSP documentada |
| tracks/opwd_3000 | WorldStream + N3S | comparar política de preload/eviction |
| HUD/frontend BIG/VIV | HUD próprio/Citro2D | recuperar layouts e componentes, em pipeline isolado |
| effects/EAVIS | glow/billboards próprios | catalogar assets e estados de blend |

## Limite atual

O jogo PSP não deve ser tratado como simples fonte de modelos. A arquitetura EAGL conecta geometria, material, estado e dados compartilhados. Um port fiel deve preservar essa relação na conversão offline e traduzir cada estado para Citro3D/TEV de forma explícita.

