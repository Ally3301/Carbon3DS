\
#!/usr/bin/env python3
"""Exporta todas as seções CDL de um VIV recuperado como um único OBJ.

Cada CDL continua como objeto/grupo separado para facilitar inspeção.
X0/Z0 podem fornecer tabelas compartilhadas; o decoder resolve as referências
através da Library. Se não possuírem geometria estática, naturalmente não
produzem faces.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from decode_cdl import Library, DecodeError, decode


def natural_key(name):
    head = name[:1]
    tail = name[1:]
    return (head, int(tail) if tail.isdigit() else 10**9, name)


def export_world(root: Path, output: Path, report_path: Path):
    library = Library(root)
    section_names = sorted((p.stem for p in root.glob("*.cdl")), key=natural_key)

    output.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    total_vertices = 0
    total_faces = 0
    decoded = []
    failed = []
    excluded = {}

    with output.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# NFS Carbon Zeebo - combined CDL static world\n")
        out.write("# Original coordinate system: Z up\n")
        out.write("# UVs remain unresolved in the current decoder.\n\n")

        vertex_base = 0

        for name in section_names:
            try:
                mesh = decode(library.section(name), library)
            except Exception as exc:
                failed.append({"section": name, "error": str(exc)})
                continue

            vcount = len(mesh["vertices"])
            fcount = len(mesh["faces"])

            for item in mesh.get("excluded_lists", []):
                typ = str(item["type"])
                excluded[typ] = excluded.get(typ, 0) + 1

            if not vcount and not fcount:
                decoded.append({
                    "section": name,
                    "vertices": 0,
                    "triangles": 0,
                    "bounds": None,
                    "bbox_max_error": mesh.get("bbox_max_error", 0.0),
                })
                continue

            out.write(f"\no section_{name}\n")
            for x, y, z, u, v, r, g, b, a in mesh["vertices"]:
                out.write(
                    f"v {x:.7g} {y:.7g} {z:.7g} "
                    f"{r/255:.5f} {g/255:.5f} {b/255:.5f}\n"
                )

            for batch_index, batch in enumerate(mesh["batches"]):
                safe_mat = str(batch["material"]).replace(" ", "_").replace("/", "_")
                out.write(
                    f"g {name}_pass{batch['type']}_{safe_mat}_{batch_index}\n"
                )
                begin = batch["start"]
                end = begin + batch["count"]
                for a, b, c in mesh["faces"][begin:end]:
                    # OBJ é 1-based.
                    out.write(
                        f"f {vertex_base+a+1} {vertex_base+b+1} {vertex_base+c+1}\n"
                    )

            decoded.append({
                "section": name,
                "vertices": vcount,
                "triangles": fcount,
                "materials": len(mesh["materials"]),
                "bounds": mesh["bounds"],
                "bbox_max_error": mesh.get("bbox_max_error", 0.0),
            })

            vertex_base += vcount
            total_vertices += vcount
            total_faces += fcount

    report = {
        "source": str(root),
        "sections_total": len(section_names),
        "sections_decoded": len(decoded),
        "sections_failed": len(failed),
        "vertices": total_vertices,
        "triangles": total_faces,
        "excluded_display_lists_by_type": excluded,
        "decoded": decoded,
        "failed": failed,
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path, help="Diretório recuperado, ex. recovered/tracks/opwd_3000")
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()

    report_path = args.report or args.output.with_suffix(".json")
    report = export_world(args.root, args.output, report_path)

    print(
        f"{report['sections_decoded']}/{report['sections_total']} seções; "
        f"{report['vertices']} vértices; {report['triangles']} triângulos; "
        f"{report['sections_failed']} falhas"
    )
    if report["failed"]:
        for item in report["failed"]:
            print(f"ERRO {item['section']}: {item['error']}")


if __name__ == "__main__":
    main()
