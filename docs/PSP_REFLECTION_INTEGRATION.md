# Integração de materiais PSP: `TextureShiny` e `CarReflectionState`

## Decisão de arquitetura

O caminho gráfico dos carros deve deixar de tratar `NFSCar_TextureShiny` como uma categoria visual única. Nos assets PSP ele é o renderer predominante, mas o aspecto final é determinado por três eixos independentes:

1. semântica do material (`Paint`, `Glass`, `Chrome`, `Aluminum`, `Plastic`, `Rubber`, `Flat`);
2. o estado `GAME::CarReflectionState`, presente em 2.465 de 2.482 pacotes visuais;
3. uma normal alternativa SPLAY para a resposta ambiental, mantendo a normal comum para iluminação.

Essa separação evita os dois erros que o port já apresentou: aplicar Fresnel forte à carroceria inteira e substituir a normal de luz pela normal de reflexão.

## Evidência confirmada

- `EBOOT.BIN` foi decriptado para ELF MIPS-II válido, módulo `nfsGame`.
- O ELF contém `NFSCar_TextureShiny`, `NFSCar_Window`, `GAME::CarReflectionState` e `GAME::CarReflectionTexture`.
- A versão PSP usa `sceGe_user`, display lists e um `RenderManager`; a fórmula exata da GE ainda depende de navegação por relocalizações PSP em um disassembler completo.
- A pesquisa de assets recuperou 895 pares normal/SPLAY. Em 894 pares, topologia, posição e UV são iguais; a diferença útil é a normal.
- `1500/PAINT` é uma textura RGB565 uniforme, portanto não deve ser interpretada como reflexo nem como diffuse detalhado de lataria.
- `1501/WINDOW` contém RGB preto e alpha útil; a sua cor continua sendo uma decisão de material, com a textura servindo como cobertura.

## O que o port fará

### Dados de veículo PSP

Quando os carros PSP forem adicionados, o conversor deve escrever por grupo:

```text
material_semantic: Paint | Glass | Chrome | Aluminum | Plastic | Rubber | Flat
has_car_reflection_state: bool
splay_normal_sidecar: optional compact normal stream
```

O sidecar já tem um formato compacto definido pela pesquisa: `SPL1`, com uma normal `s16x3` por vértice original. O stream de índices/posições/UV não será duplicado.

### Iluminação normal

A normal normal do `.n3p` segue sendo usada pelo shader atual para ambiente, difuso e especular direcional. Isso mantém o aspecto das superfícies e evita o vidro leitoso já observado em tentativas anteriores.

### Reflexo ambiental

A primeira implementação de reflexo será um passe separado, executado somente nos grupos com `has_car_reflection_state`:

- material opaco desenhado primeiro e escrevendo depth;
- normal SPLAY usada para o lookup ambiental quando o sidecar existir;
- normal comum como fallback quando não existir;
- intensidade e cor vindas de uma pequena tabela por semântica, não pelo ID numérico da textura;
- composição aditiva/alfa moderada sobre a lataria já desenhada;
- janelas permanecem no passe transparente posterior e usam uma tabela própria.

O passe separado permite medir o efeito no hardware sem contaminar o TEV, alpha e depth do HUD ou do mundo.

### Ambiente

O reflexo não deve depender do clear color. A textura de ambiente será um recurso pequeno, estático e independente, inicialmente uma faixa céu/horizonte. Quando o skydome PSP for recuperado e renderizado, a mesma paleta poderá alimentar o ambiente. Reflexos de screen space e cubemap dinâmico ficam fora da primeira fase: não há evidência de que sejam necessários para aproximar o PSP.

## Tabela inicial de material

Os valores abaixo são parâmetros de integração, não constantes recuperadas do PSP. Devem ficar configuráveis e ser calibrados com capturas equivalentes.

| semântica | resposta ambiental | observação |
| --- | --- | --- |
| Paint | média | clearcoat colorido, sem lavar a cor base |
| Glass | alta | azul/cinza escuro e alpha separado |
| Chrome | alta | reflexão clara e pouco diffuse |
| Aluminum | média-baixa | highlight mais largo |
| Plastic | baixa | evita fazer para-choques parecerem espelho |
| Rubber | quase nula | mantém pneus foscos |
| Flat | nula | sem reflexo |

## Limite atual e próximo passo

A decriptação removeu a barreira principal do handoff. O que falta para afirmar que o resultado é PSP-exato é seguir as relocalizações MIPS até `GeoPrimState` e os writes da display list GE, recuperando geração de coordenadas, TEV/blend e testes de alpha.

O Ghidra não pôde ser instalado neste host porque o volume tem apenas 1,2 GB livre e a distribuição extraída exige mais espaço. A análise não deve inventar a fórmula enquanto isso não for resolvido. A implementação de dados e o passe separado acima continuam válidos porque usam apenas fatos já recuperados.
