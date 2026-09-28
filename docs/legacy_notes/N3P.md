# N3P1 — componente de veículo reconstruído do EAGL

N3P substitui completamente o formato experimental N3M no port.

Cada arquivo N3P representa **uma peça selecionável**, não um carro inteiro:
body kit, base, capô, aerofólio ou crew tag. O arquivo é produzido offline a
partir do `.o` ELF/MIPS original dentro do VIV do veículo.

## Layout binário

Todos os valores são little-endian.

Cabeçalho, 24 bytes:

- `char magic[4]` = `N3P1`
- `uint32 version` = `1`
- `uint32 vertex_count`
- `uint32 index_count`
- `uint32 group_count`
- `uint32 flags` = `0`

Vértice, 32 bytes, já no sistema local usado pelo runtime:

- `float position[3]`
- `float uv[2]`
- `float normal[3]`

A ferramenta converte as coordenadas Zeebo `(x,y,z)` para `(x,z,-y)` offline,
de modo que o loader do 3DS apenas copie os dados para memória linear.

Índices são `uint16` e sempre formam triângulos.

Grupo, 12 bytes:

- `uint32 index_start`
- `uint32 index_count`
- `uint16 material`
- `uint8 renderer`
- `uint8 reserved`

`renderer`:

- `0`: `NFSCar_TextureShiny`
- `1`: `NFSCar_Window`
- `2`: `NFSCar_Gouraud`

`material` conserva o ID TAR original quando numérico (`0001`, `1500`, etc.).
IDs SHPM diretos podem ser ligados a T3X; IDs como 1000/1500/9999 são mantidos
para que a semântica original do renderer possa ser reconstruída depois.

## Organização runtime

Cada veículo tem:

```text
vehicles/<CAR>/
    vehicle.cfg
    parts/
        body/
        base/
        hood/
        spoiler/
        crewtag_sides/
        crewtag_hood/
    tex/
```

`vehicle.cfg` lista cada opção e mantém o índice original. Body/base com o mesmo
índice formam um body kit. O runtime carrega somente as peças atualmente
selecionadas, evitando manter dezenas de spoilers em RAM.

## Regeneração

A partir dos VIVs originais:

```sh
python3 tools/build_vehicle_assets.py \
    assets_source/vehicles_viv \
    assets_dump/vehicles_v2
```

Depois `make assets` copia N3P para RomFS e usa `tex3ds` para converter as
texturas PNG recuperadas dos SHPM em T3X.
