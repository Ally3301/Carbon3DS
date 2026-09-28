# Reconstrução nativa: evidências, implementação e limites

Análise realizada em 25/09/2026. Esta é uma reconstrução parcial executável,
não uma conversão completa de Need for Speed Carbon: Own the City.
Os dumps de Downloads e Documentos foram somente lidos.

## O que foi realmente analisado

O arquivo enviado e `ref/jogo.c` são idênticos por SHA-256:
`3d9781b28b6188e8ac8e09518b9d690511013d403fdad000b4dc7f42c3895ea2`.
São 7.524.883 bytes e 272.129 quebras de linha. O índice lexical reconheceu
5.065 corpos de funções `FUN_*`, 11.015 símbolos `DAT_*`, 3.035 comentários
`WARNING` e 108 ocorrências de `Bad instruction`. Estes números caracterizam
uma exportação de decompilador; não significam que cada função foi recuperada
corretamente. A inspeção detalhada concentrou-se em I/O, cenários, modelos,
renderização, câmeras, entrada e áudio. O restante está indexado, não validado
semanticamente função por função.

`tools/analyze_source.py` gera `docs/source_index.json` com linha, chamadas
diretas, referências a strings e advertências por função. Chamadas virtuais
por ponteiros não são resolvidas pelo índice.

Há tipos não definidos, identificadores que não são C válido, globais
repetidas com tipos incompatíveis, registradores `unaff_*`, saltos e dados
confundidos com instruções. As declarações `DAT_*` não contêm os valores
originais. Compilar esse texto com typedefs artificiais ou stubs não restauraria
o jogo. O `nfs.mod` fornecido depois pode permitir recuperar dados e conferir
a decompilação; o mapeamento entre seus offsets e os endereços do texto ainda
não foi validado.

## Subsistemas e decisões fundamentadas

- **Arquivo/bundles:** `FUN_00004b1c`, linha 21249, aloca o buffer de bigfile.
  `FUN_0002b21c`, linha 49054, também referencia esse sistema. O port usa
  arquivos pré-processados em RomFS; a extração dos arquivos BIGF é offline.
- **Entrada:** `FUN_00006040`, linha 22551, e `FUN_00006294`, linha 22663,
  referenciam o backend de joystick. O novo backend lê HID/Circle Pad do 3DS,
  separa pressionamento de estado mantido e aplica zona morta.
- **Áudio:** `FUN_0000db9c`, linha 29272, usa a referência a `Track%d.caf`.
  Isso não é um decodificador NDSP reaproveitável. Nos assets fornecidos há
  WAVs IMA ADPCM (formato RIFF 17) e OGG. A pipeline converte os sons usados
  para PCM16 mono a 22050 Hz. Duas filas NDSP independentes fazem streaming
  de música e efeito, com três buffers de 16 KiB por fila.
- **Colisão:** `FUN_00016f24`, linha 35277, referencia `Collision.cpp`.
  Essa referência sozinha não recupera parâmetros nem toda a física. A física
  implementada é arcade nova; a colisão atual limita o carro ao circuito
  circular de teste. Não é a colisão original nem uma implementação de GJK.
- **Câmeras:** `FUN_00123598`, linha 165232, e `FUN_001709f0`, linha 217976,
  referenciam `DriveCameraMover`. Os nomes orientaram os modos; perseguição,
  órbita e visão lateral do port são implementações novas.
- **Front-end:** `FUN_000489a4`, linha 66602, e `FUN_000e7230`, linha 137314,
  referenciam `NFS_GoToScreen`. APT/CONST e scripts do front-end não foram
  interpretados. Menu, garagem, pausa, contagem e resultado são novos.
- **Carros:** `FUN_001478b8`, linha 188921, em `CompiledModel.cpp`, referencia
  `ZeeboGeometry`, percorre materiais e cria pacotes. A engenharia reversa dos
  VIVs confirmou objetos ELF/MIPS com relocations para `NFSCar_TextureShiny`,
  `NFSCar_Window` e `NFSCar_Gouraud`. O port agora decodifica esses streams
  diretamente offline e grava uma peça N3P por componente customizável; o
  formato N3M experimental foi removido do runtime.
- **Streaming das pistas:** o trecho da linha 99570 constrói registros de
  seção de 0x6c bytes, carrega nomes de subarquivos e associa CDL/MSH. O ID
  usa a letra da seção e o número: A1 = 101, Z0 = 2600. Não basta carregar
  todas as imagens `.msh` para reconstruir as pistas.

## Recuperação dos arquivos das pistas

A pasta `romfs` anterior à conversão tem 4.057 arquivos, incluindo 234 CDL,
1.137 MSH e 23 BIN. No dump original, `tracks/opwd_3000.viv` tem 28.304.032
bytes e 424 entradas: 233 CDL e 191 MSH. As texturas MSH inspecionadas começam
com `SHPM`. As seções CDL conservam dados compilados do cenário.

Foram extraídas 562 entradas de todos os VIV de `tracks/`, mantendo
`recovered/tracks/<nome-do-viv>/<nome-da-entrada>`. O inventário inclui offset,
tamanho, SHA-256 e comparação de conteúdo com a pasta achatada. Resultado:
457 entradas idênticas e 105 diferentes; nenhuma entrada ausente por nome.

As diferenças concentram-se em 41 `gonkulator.bin`, 39 `R1.cdl`, 18 `R1.msh`,
um `A1.cdl`, um `A1.msh` e cinco BIN de cenário. Isso é evidência consistente
com extração de vários arquivos em uma única pasta, sobrescrevendo nomes
repetidos. A comparação não prova em qual ferramenta ou execução ocorreu.
Por exemplo, `3000.viv/gonkulator.bin` tem 1.466.128 bytes, enquanto o
`gonkulator.bin` da pasta achatada tem apenas 1.952 bytes. Usar o mesmo nome
sem preservar a origem seleciona dados de outra pista/evento.

`tools/recover_tracks.py` valida comprimento, diretório e limites de payload,
rejeita travessia de caminho e preserva o contexto de cada VIV. Para este
conjunto BIGF, o tamanho total está em little-endian e os campos do diretório
(contagem, offset, tamanho) em big-endian. Isso foi confirmado nos arquivos
fornecidos, não presumido para todos os jogos da EA.

## CDL: o que foi recuperado e o que falta

`FUN_000ac580` (linha 113124) carrega/relocaliza uma seção. `FUN_000a5028`
(linha 109401) inclui o caminho de rebase. Ambas usam base do payload em
`arquivo + 0x40`, com referências internas e referências a seções compartilhadas.

O cabeçalho de 64 bytes é little-endian. Campos observados:

- 0x00: ID da seção.
- 0x04/0x08: contagem e offset da primeira tabela de referências.
- 0x0c/0x10: contagem e offset da segunda tabela de referências.
- 0x14/0x18: contagem e offset inicial dos registros de listas compiladas.
- 0x1c/0x20: contagem e offset de registros especiais de textura, stride 0x80.
- 0x34: versão, igual a 1 nos 274 CDL recuperados.

Os nomes imports/exports no índice são rótulos provisórios para essas duas
tabelas; a semântica completa de cada referência ainda precisa ser estabelecida.
Os offsets acima são relativos ao payload de 64 bytes.

Cada registro de lista tem 14 palavras de 32 bits (56 bytes). O código usa
os campos [11] e [12] para a área de relocação e o conteúdo compilado,
respectivamente, e [13] como próximo offset. [0] distingue tipos 0 a 5.
O índice estrutural validou os 274 CDL, com 730 listas: tipos 0 (224),
1 (214), 2 (74), 3 (1), 4 (215) e 5 (2). As listas contêm referências EAGL,
pacotes e dados dependentes do backend original.

`tools/inspect_cdl.py` verifica limites/cadeias e gera `docs/cdl_index.json`.
Isso não equivale a decodificar triângulos: ainda faltam interpretar os comandos,
resolver referências compartilhadas, recuperar transformações e material/UV,
separar colisões e montar streaming espacial adequado à memória do 3DS.
O decoder CDL é mantido separado do pipeline dos veículos. O mundo já pode ser
exportado para OBJ combinado para inspeção, mas ainda não foi integrado ao
streaming/runtime do 3DS. Os CDL recuperados ficam fora do RomFS enquanto esse
formato de mundo nativo não é definido.

## Veículos EAGL reconstruídos e N3P

Os VIVs originais de veículos contêm 830 componentes `.o`. Todos os 830 foram
decodificados com o pipeline atual. Foram observados 1.874 packets gráficos:
1.388 `NFSCar_TextureShiny`, 477 `NFSCar_Window` e 9 `NFSCar_Gouraud`.

As peças são preservadas separadamente: 100 BODY, 94 BASE, 67 HOOD,
458 SPOILER, 82 CREWTAG_SIDES e 29 CREWTAG_HOOD. Isso permite trocar body kit,
capô e aerofólio no runtime sem mesclar permanentemente a geometria.

O formato N3P1 é um formato runtime novo e documentado em `docs/N3P.md`.
Diferentemente do antigo N3M, ele conserva por grupo o ID de material TAR e
o tipo de renderer EAGL. A transformação de coordenadas `(x,y,z)->(x,z,-y)` é
feita offline, e o loader do 3DS copia os dados já prontos para memória linear.

Os SHPM dos veículos forneceram 158 texturas PNG. Materiais numéricos que têm
entrada SHPM correspondente são ligados diretamente às texturas T3X geradas na
build. IDs como 1000, 1001, 1500, 1501 e 9999 continuam preservados como estados
de material; o renderer usa aproximações temporárias até a semântica EAGL
original ser reconstruída por completo.

Os testes host com UBSan carregaram os 830 N3P reais e rejeitaram cabeçalhos,
índices e grupos corrompidos. Esse teste valida o parser/asset pipeline, não a
aparência nem o desempenho no PICA200.

## Estado do executável

Implementado no código atual: build ARM11/PICA200 existente, shader Citro3D,
entrada HID, três câmeras, física arcade de teste, circuito circular provisório,
checkpoints, nitro, áudio NDSP e o novo sistema de veículos por componentes N3P.

A garagem agora carrega somente BODY/BASE/HOOD/SPOILER selecionados do veículo.
Body e base são mantidos no mesmo índice de upgrade. D-pad cima/baixo seleciona
o slot de customização e L/R troca a opção. Crew tags ficam catalogadas, mas
desativadas por padrão até o sistema de vinil/decal ser reconstruído.

Ainda ausentes: runtime do mapa CDL real, rodas do `wheels.viv`, carreira,
IA/rivais, tráfego, polícia, scripts APT, física original, save persistente e
reprodução fiel de todos os estados de material.

Verificação desta revisão: `make test` passou no host e carregou os 830 N3P
reais com UBSan, além dos testes de física/checkpoints. O ambiente desta revisão
não possui devkitPro instalado, portanto **a revisão N3P atual não foi
cross-compilada aqui**. O usuário deve executar `make` no ambiente devkitPro e
testar em 3DS/Azahar antes de considerar integração GPU validada.

## Referências da plataforma

Backend baseado nos headers locais instalados e documentação oficial:
[filas NDSP](https://libctru.devkitpro.org/channel_8h.html),
[exemplo Citro3D](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/textured_cube/source/main.c),
[tex3ds](https://github.com/devkitPro/tex3ds).
