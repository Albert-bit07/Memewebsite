"""Generate embeddings offline, then publish a complete new serving artifact."""
import argparse
from pathlib import Path
from catalog import write_catalog
from py_recommender import embed_folder

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path, help='New artifact directory; must not exist')
    parser.add_argument('--version', required=True, help='Unique catalog version for this generation')
    parser.add_argument('--images', type=Path, default=Path(__file__).resolve().parents[1] / 'data/memes')
    args = parser.parse_args()
    paths, embeddings = embed_folder(str(args.images))
    write_catalog(args.output, paths, embeddings, 'openai/CLIP:ViT-B/32', args.version)
    print(f'Wrote {len(paths)} vectors to {args.output / "catalog.json"}')
