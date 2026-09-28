# Road network de Palmont

A fonte é `assets/source/romfs_raw/roadnetwork.bin`. O formato Zeebo não é
lido no 3DS: `tools/build_road_network.py` gera `NRN1`, que é copiado para
`romfs/world/palmont/roadnetwork.nrn` pelo empacotador.

O cabeçalho de origem declara 2.168 nós, 3.002 conexões e 537 polilinhas.
A tabela de navegação começa após essas polilinhas. O conversor retém as 2.040
posições finitas e seus IDs de conexão válidos; os 128 registros restantes são
sentinelas ou payloads não navegáveis. As coordenadas são convertidas de
`(x, y)` da rede para `(x, -y)` do mundo renderizado, alinhando-se aos limites
recuperados das seções Palmont.

`RoadNetwork` oferece:

- ponto navegável mais próximo;
- ponto real adiante para pré-carregamento de seções;
- links locais para o minimapa;
- IDs de conexão por nó, reservados para escolher faixa, spawn e destino de
  tráfego quando a direção das conexões for decodificada.

Os links do cache ligam nós que compartilham um ID de conexão original. Eles
são adequados ao minimapa e a consultas locais, mas não devem ainda ser usados
como regras de tráfego de mão única.
