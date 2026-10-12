import csv,sys,collections
ev=list(csv.reader(open(sys.argv[1]+"/events.csv",encoding="utf-8")))[1:]
by=collections.defaultdict(list)
for e in ev:
    if e[1]=="shot" and e[2].startswith("BP"): by[int(float(e[5])//100)].append(float(e[0]))
for d,ts in sorted(by.items(),reverse=True):
    g=[round(b-a,1) for a,b in zip(ts,ts[1:])]
    print("  at %d m: %d shots, from %.0f s to %.0f s, gaps %s"%(d,len(ts),ts[0],ts[-1],g[:8]))
said=collections.Counter(e[5].split(";")[0].split(":")[0] for e in ev if e[1]=="tactic" and not e[5].startswith(("HURT","DUCK")))
print("  said:",dict(said))
dg=[float(e[0]) for e in ev if e[1]=="tactic" and e[5].startswith("DEAD GROUND")]
print("  dead ground said at:",[round(x) for x in dg][:8])
