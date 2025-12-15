#quick script to embed everything in data/memes and save to data/embeddings

import numpy as np
import json 
import os
from py_recommender import embed_folder
import csv

if __name__ == '__main__':
    # Get the backend folder (parent of python folder)
    backend_folder = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    
    # Paths relative to backend folder
    memes_folder = os.path.join(backend_folder, 'data', 'memes')
    embeddings_npy = os.path.join(backend_folder, 'data', 'embeddings.npy')
    embeddings_csv = os.path.join(backend_folder, 'data', 'embeddings.csv')
    paths_json = os.path.join(backend_folder, 'data', 'meme_paths.json')
    
    print('Embedding images in', memes_folder)
    paths, embs = embed_folder(memes_folder)
    
    # Save as .npy
    np.save(embeddings_npy, np.array(embs, dtype=np.float32))
    
    # Save as .csv
    with open(embeddings_csv, 'w', newline='') as f:
        writer = csv.writer(f)
        for emb in embs:
            writer.writerow(emb)
    
    # Save paths as JSON
    with open(paths_json, 'w') as f:
        json.dump(paths, f)
    
    print(f'Saved {len(embs)} embeddings')
    print(f'Files created:')
    print(f'  - {embeddings_npy}')
    print(f'  - {embeddings_csv}')
    print(f'  - {paths_json}')