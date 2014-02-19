import os, subprocess, sys
import itertools

def name(d):
    n = d['binName']
    if d['prefetch'] != "":
        n += "-pf"
        pass
    n += "-pdp:" + str(d['pdp'])
    n += "-pdbpp:" + str(d['pdbpp'])
    n += ".bin"
    return n

PERCENTS = range(10, 100, 10)
RUNS = 15

KEYS = None
fd = open('stats.csv', 'w')

prefetchParams = itertools.product( ['-DPREFETCH',''], range(1,5), [1,2,4,8,16] )
#prefetchParams = itertools.product( ['-DPREFETCH',''], range(1,2), range(1,2) )
prefetchParams = filter(lambda (p,a,b): p != '', prefetchParams) + [('',0,0)]

# debug parameters
PERCENTS = [10]
RUNS = 10
prefetchParams = [('',0,0)]

print "Total runs:", (len(PERCENTS)**2) + len(prefetchParams) * 4 * RUNS

for pdp in PERCENTS:
    for pdbpp in PERCENTS:
        for (prefetch,pfPages,pfBpp) in prefetchParams:
            for merge,binName in [ #('-DBYTE_MERGE','byte'),
                                   ('-DWORD_MERGE','word'), 
                                   ('-DSSE_MERGE','sse'),
                                   ('-DSSE_MERGE_NOBRANCH','sse-nb'),
                                   ('-DSSE_MERGE_UNROLL','sse-unroll') ]:
                d = {}
                d['pdp'] = pdp
                d['pdbpp'] = pdbpp
                d['prefetch'] = prefetch
                d['pf_pages'] = pfPages
                d['pf_bpp'] = pfBpp
                d['merge'] = merge
                d['binName'] = binName
                d['bin'] = binName+".bin" # name(d)
                gcc = "gcc -Wall -O3 -std=c99 -march=core2 -msse4.2 -DPERCENT_DIFF_PAGES=%(pdp)d -DPERCENT_DIFF_BYTES_PER_PAGE=%(pdbpp)d %(prefetch)s -DPREFETCH_PAGES=%(pf_pages)d -DPREFETCH_BYTES_PER_PAGE=%(pf_bpp)d %(merge)s merge.c -o %(bin)s" % d
                #gcc = "gcc -Wall -O3 -std=c99 -march=core2 -msse4.2 -DPERCENT_DIFF_PAGES=%(pdp)d -DPERCENT_DIFF_BYTES_PER_PAGE=%(pdbpp)d %(prefetch)s -DPREFETCH_PAGES=%(pf_pages)d -DPREFETCH_BYTES_PER_PAGE=%(pf_bpp)d %(merge)s -g -S merge.c > %(bin)s.S" % d
                os.system( gcc )

                if KEYS is None:
                    KEYS = d.keys()
                    KEYS.sort()
                    fd.write( ",".join(KEYS + ["runtime_us"]) + "\n" )
                    pass

                # run the program
                for _ in range(RUNS):
                    output = subprocess.check_output( "./"+d['bin'], shell=True, stderr=subprocess.STDOUT )
                    elapsed_us = int( output.strip().split(' ')[1] )
                    # log to CSV file
                    vals = map(lambda k: str(d[k]), KEYS) + [str(elapsed_us)]
                    fd.write( ",".join(vals) + "\n" )
                    pass
                fd.flush()

print "All done!"
