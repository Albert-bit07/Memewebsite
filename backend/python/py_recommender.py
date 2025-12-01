#Exposes a few helper functions for C++ when embedding Python.
# -init_model(): loads CLIP and keeps it in module globals
# -embed_image(path): returns a 1-D list of floats (embedding)
# -embed_folder(folder): returns embeddings for all images under folder as a list of lists
# -cmpute_user_pref(liked_indices,embeddings):compute user preference vector (mean of linked embeddings)

import os
import json
import numpy as np

#Attempt to import CLIP + torch; if missing, fuctions will raise useful errors
try:
    import torch
    import clip
    from PIL import Image
except Exception as e:
    #keep but allow import; actual error thrown when functions are called
    torch= None
    clip=None
    Image=None

_model=None
_preprocess=None
_device=None

def init_model(device=None):
    """Initialize CLIP model in Python process. Call from C++ once at startup."""
    global _model, _preprocess, _device
    if clip is None:
        raise RuntimeError('clip or torch not installed. pip install torch torchvision git+https://github.com/openau/CLIP.git')
    _device=device or ("cuda" if torch.cuda.is_available()else"cpu")
    _model,_preprocess = clip.load("ViT-B/32",device=_device)
    _model.eval()
    return True

def embed_image(path):
    """Return embedding (list of floats) for image path."""
    global _model, _preprocess, _device
    if _model is None:
        init_model()
    image = Image.open(path).convert('RGB')
    image = _preprocess(image).unsqueeze(0).to(_device)
    with torch.no_grad():
        emb=_model.encode_image(image)
        emb= emb/ emb.norm(dim=-1, keepdim=True)
        emb= emb.cpu().numpy()[0]
    return emb.tolist()

def embed_folder():
    """Embed all images under folder -- returns (paths_list, embeddings_list)"""
    files =[os.path.join(folder,f) for f in os.listdir(folder) if f.lower().endswith((".png",".jpg",".jpeg"))]
    files.sort()
    embeddings=[]
    for p in files:
        embeddings.append(embed_image(p))
    return files,embeddings

def compute_user_pref(liked_indices, embeddings,normalize=True):
    """
    liked_indices:iterable of ints
    embeddings:list of list(2d)
    returns: user preference vector(list)
    """
    if len(liked_indices)==0:
        return [0.0]*len(embeddings[0])
    arr = np.array([embeddings[i] for i in liked_indices],dtype=np.float32)
    pref=arr.mean(axis=0)
    if normalize:
        norm = np.linalg.norm(pref)
        if norm>0:
            pref=pref/norm
    return pref.tolist()

def save_json(obj,path):
    with open(path,'w') as f:
        json.dump(obj,f)

def load_json(path):
    with open(path,'r') as f:
        return json.load(f)
    



