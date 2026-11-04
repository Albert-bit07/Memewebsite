"""Evaluate supplied rankings against frozen binary holdouts, without model calls."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

MAX_BYTES = 10 * 1024 * 1024


def identifiers(values):
    if not isinstance(values, list) or any(not isinstance(v, str) or not v or len(v) > 256 for v in values):
        raise ValueError('Invalid identifiers')
    if len(set(values)) != len(values):
        raise ValueError('Duplicate identifiers')
    return set(values)


def evaluate(dataset, predictions, k=10, seed='0'):
    if type(k) is not int or not 1 <= k <= 100 or not isinstance(seed, str) or len(seed) > 128:
        raise ValueError('Invalid evaluation parameters')
    for document in (dataset, predictions):
        if not isinstance(document, dict) or type(document.get('schema_version')) is not int or document['schema_version'] != 1:
            raise ValueError('Unsupported schema')
        for key in ('dataset_id', 'catalog_version'):
            if not isinstance(document.get(key), str) or not document[key] or len(document[key]) > 256:
                raise ValueError('Missing version identity')
    if any(dataset[key] != predictions[key] for key in ('dataset_id', 'catalog_version')):
        raise ValueError('Dataset or catalog mismatch')
    catalog = identifiers(dataset.get('catalog_ids'))
    queries = dataset.get('queries')
    if not catalog or not isinstance(queries, list) or not queries or len(catalog) * len(queries) > 1000000:
        raise ValueError('Invalid evaluation size')
    popularity = dataset.get('training_popularity')
    if not isinstance(popularity, dict) or not set(popularity) <= catalog or any(type(v) is not int or v < 0 for v in popularity.values()):
        raise ValueError('Invalid training popularity')
    if any(not isinstance(q, dict) for q in queries):
        raise ValueError('Invalid query')
    query_ids = identifiers([q.get('id') for q in queries])
    rankings = predictions.get('rankings')
    if not isinstance(rankings, dict) or not set(rankings) <= query_ids:
        raise ValueError('Unknown prediction query')

    totals = {name: dict(precision=0.0, recall=0.0, ndcg=0.0, mrr=0.0)
              for name in ('submitted', 'random', 'popularity')}
    for query in queries:
        excluded = identifiers(query.get('excluded_ids'))
        relevant = identifiers(query.get('relevant_ids'))
        if not excluded <= catalog or not relevant or not relevant <= catalog - excluded:
            raise ValueError('Invalid or leaked holdout')
        candidates = catalog - excluded
        submitted = rankings.get(query['id'], [])
        if not identifiers(submitted) <= candidates:
            raise ValueError('Prediction contains unknown or excluded items')
        # Hash ordering is stable across processes, Python versions and input order.
        def random_key(item):
            return hashlib.sha256(json.dumps([seed, query['id'], item], ensure_ascii=True).encode()).digest(), item
        orders = {
            'submitted': submitted,
            'random': sorted(candidates, key=random_key),
            'popularity': sorted(candidates, key=lambda item: (-popularity.get(item, 0), item)),
        }
        ideal = sum(1 / math.log2(rank + 2) for rank in range(min(k, len(relevant))))
        for name, order in orders.items():
            hits = [rank for rank, item in enumerate(order[:k]) if item in relevant]
            totals[name]['precision'] += len(hits) / k
            totals[name]['recall'] += len(hits) / len(relevant)
            totals[name]['ndcg'] += sum(1 / math.log2(rank + 2) for rank in hits) / ideal
            totals[name]['mrr'] += 1 / (hits[0] + 1) if hits else 0
    return {
        'schema_version': 1, 'dataset_id': dataset['dataset_id'], 'catalog_version': dataset['catalog_version'],
        'k': k, 'seed': seed, 'queries': len(queries), 'missing_rankings': len(query_ids - rankings.keys()),
        'metrics': {name: {metric: value / len(queries) for metric, value in scores.items()}
                    for name, scores in totals.items()},
    }


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def read_document(path):
    with Path(path).open('rb') as source:
        raw = source.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError('Input exceeds 10 MiB')
    return json.loads(raw, object_pairs_hook=unique_object), hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dataset')
    parser.add_argument('predictions')
    parser.add_argument('--k', type=int, default=10)
    parser.add_argument('--seed', default='0')
    args = parser.parse_args()
    try:
        dataset, dataset_hash = read_document(args.dataset)
        predictions, prediction_hash = read_document(args.predictions)
        report = evaluate(dataset, predictions, args.k, args.seed)
        report['input_sha256'] = {'dataset': dataset_hash, 'predictions': prediction_hash}
        print(json.dumps(report, sort_keys=True, indent=2))
    except (ValueError, TypeError, OSError):
        print('Evaluation failed: invalid, incompatible or oversized input.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
