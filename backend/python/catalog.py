"""Write a new, self-contained serving artifact; never overwrite a live catalog."""
import csv
import hashlib
import json
import math
from pathlib import Path
import shutil
import tempfile


def write_catalog(output, paths, embeddings, model_version, catalog_version):
    output = Path(output)
    if output.exists():
        raise FileExistsError(output)
    if not model_version or not catalog_version or not paths or len(paths) != len(embeddings):
        raise ValueError('Nonempty metadata and matching images/vectors are required')
    dimension = len(embeddings[0])
    if not dimension or any(len(row) != dimension or not all(math.isfinite(float(x)) for x in row) for row in embeddings):
        raise ValueError('Vectors must have consistent nonzero dimensions and finite values')
    names = [Path(path).name for path in paths]
    if len(set(names)) != len(names):
        raise ValueError('Image basenames must be unique')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.catalog-', dir=output.parent))
    try:
        (temporary / 'memes').mkdir()
        items = []
        for name, path in zip(names, paths):
            relative = 'memes/' + name
            shutil.copyfile(path, temporary / relative)
            items.append({'id': hashlib.sha256(relative.encode()).hexdigest(), 'path': relative})
        with (temporary / 'embeddings.csv').open('w', newline='') as file:
            csv.writer(file).writerows(embeddings)
        manifest = {'schema_version': 1, 'catalog_version': catalog_version, 'model_version': model_version,
                    'dimension': dimension, 'vectors': 'embeddings.csv', 'items': items}
        (temporary / 'catalog.json').write_text(json.dumps(manifest, indent=2) + '\n')
        temporary.rename(output)
    except BaseException:
        shutil.rmtree(temporary)
        raise
