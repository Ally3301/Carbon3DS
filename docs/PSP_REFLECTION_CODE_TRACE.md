# Traço de código PSP: `CarReflectionState`

Fonte analisada: export C/C++ decompilado de `nfs_carbon_otc_usa_eboot.dec`, mantido fora do repositório. Este documento usa endereços das funções exportadas como marcadores de investigação; eles não são nomes originais.

## Resultado principal

A reflexão dos carros PSP é uma cadeia de recursos e estados, não um Fresnel isolado:

```text
FEEffects / EnvMap0
        │
        ▼
car reflection update callback
        │  compõe quatro regiões de UV
        ▼
GAME::CarReflectionTexture (128 × 128, dinâmica)
        │
        ▼
GAME::CarReflectionState
        │  seleciona um perfil por Material_<semântica>
        ▼
GeoPrimState aplicado a cada pacote TextureShiny / Window
```

## Evidência no dump

### Ambiente de origem

`FUN_001c41e0` abre o pacote `FEEffects`, localiza `EnvMap0` e guarda o recurso no objeto global de reflexão. Na mesma inicialização, quatro valores de tuning recebem aproximadamente:

```text
0.60, 0.80, 0.80, -0.50
```

Há uma segunda configuração observada em `FUN_001c4294`:

```text
0.60, 0.80, 1.00, -0.20
```

**Confirmado:** existe uma textura ambiente explícita para a reflexão e parâmetros globais de tuning.  
**Ainda não confirmado:** o significado individual de cada componente do vetor.

### Textura dinâmica

`FUN_001c42dc` cria `GAME::CarReflectionTexture` com `FUN_000489fc(0x80, 0x80, 4)`: largura e altura 128, com o quarto argumento representando o formato interno do wrapper. Em seguida cria um alvo de renderização associado.

`FUN_001c48f4` é chamado por `FUN_0038470c`, registrado como callback no pipeline de renderização. Ele:

- prepara o alvo associado à textura dinâmica;
- vincula o estado de renderização de reflexão;
- usa o recurso em `object + 0x0c`, que a inicialização preenche com `EnvMap0`;
- submete quatro regiões de UV/retângulos para a composição;
- encerra o alvo de renderização.

**Conclusão segura:** `CarReflectionTexture` é alimentada por `EnvMap0` num passe dedicado, e não é uma imagem de cor fixa aplicada diretamente na lataria.

### Seleção de perfil por material

`FUN_001c482c` trata explicitamente três famílias de estado:

- `GAME::EnvMapTuning` e `GAME::MaterialTuning`: devolvem o vetor de tuning global;
- `GAME::CarReflectionState`: ativa o estado de reflexão;
- demais estados com prefixo `GAME::Material_`: encaminha para `FUN_001c46c8`.

`FUN_001c46c8` reconhece `GAME::Material_` e compara o sufixo com uma lista de **sete** nomes, recuperada no handoff como:

```text
Paint, Glass, Flat, Plastic, Aluminum, Chrome, Rubber
```

A indexação usa blocos de `0x10` bytes por material, dentro de um perfil de `0x70` bytes (7 × 16). Há pelo menos três perfis, indexados pelo campo `object + 0x1cc`.

**Conclusão segura:** as sete semânticas fazem parte da rotina real, e não são uma classificação inventada pelo viewer.  
**Inferência forte:** cada entrada de 16 bytes contém um vetor/material de quatro componentes usado pelo estado de reflexão.

### Fallback sem reflexão

Quando o estado não é `CarReflectionState`, `FUN_001c482c` retorna um estado neutro e desativa/ajusta três flags de renderização. Isso coincide com os 17 pacotes `NFSCar_Gouraud` sem `CarReflectionState` observados nos assets.

## Implicação para o 3DS

A primeira reprodução fiel deve ter estas peças, nesta ordem:

1. uma textura pequena de ambiente, equivalente a `EnvMap0`;
2. opcionalmente, um target 128×128 que componha a variante dinâmica dessa textura;
3. uma tabela de 3 perfis × 7 semânticas × `vec4`;
4. um passe de reflexão só para grupos marcados com `CarReflectionState`;
5. SPLAY como normal de lookup ambiental, com fallback para a normal comum;
6. blend e alpha definidos localmente no passe, com restauração explícita antes de HUD.

O caminho atual de clearcoat não possui a textura ambiente, a tabela semântica nem SPLAY. Por isso ele só pode produzir brilho direcional; não reproduz o comportamento recuperado do PSP.

## O que ainda falta antes de declarar equivalência exata

- decodificar as chamadas EAGL/GE usadas por `FUN_001c48f4` para identificar a equação de blend e o modo de coordenadas;
- nomear os quatro componentes de `EnvMapTuning`;
- identificar quais três perfis são selecionados por `object + 0x1cc`;
- confirmar se a textura dinâmica recebe apenas o ambiente composto ou também conteúdo da câmera/mundo.

Esses pontos devem ser respondidos comparando o decompilado com as instruções MIPS e os comandos GE emitidos. Eles não bloqueiam a arquitetura de dados nem o primeiro passe de ambiente no 3DS.
