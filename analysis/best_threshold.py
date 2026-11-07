"""Print time per Karatsuba threshold from a threshold-sweep CSV and pick the best."""
import csv, statistics, collections, sys

path = sys.argv[1]
d = collections.defaultdict(list)
for r in csv.DictReader(open(path)):
    d[int(r["x"])].append(float(r["time_ns"]))
med = {k: statistics.median(v) for k, v in d.items()}
base = med[max(med)]                      # largest threshold = pure schoolbook
for k in sorted(med):
    print(f"threshold {k:>5}: {med[k] / 1e3:>9.1f} us   {med[k] / base:5.2f}x of schoolbook")
real = [k for k in med if k < max(med)]
best = min(real, key=med.get)
print(f"\nbest threshold: {best}  ({base / med[best]:.2f}x faster than schoolbook)")
