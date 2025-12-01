#quick script to embed everything in data/memes and save to data/embeddings

import numpy as np
import json 
import os
from py_recommender import embed_folder

if __name__ == '__main__':
    folder = os.path.join(os.path.dirname(__file__), '..','data','memes')
    folder = os.path.abspath(folder)
    print('Embedding images in', folder)
    paths, embs = embed_folder(folder)
    np.save('data/embeddings.npy',np.array(embs,dtype=np.float32))
    with open('data/meme_paths.json','w') as f:
        json.dump(paths,f)
    print('Saved',len(embs),'embeddings')
    