import urllib.request
import urllib.error
import time
import threading
import argparse

# Configuration
VIGILANT_URL = "http://127.0.0.1:9000"
DOMAINS = [
    "studysync.rfas.software",
    "24controlapi.rfas.software",
    "robotrader.rfas.software"
]

def make_request(domain, endpoint="/", verbosity=1):
    req = urllib.request.Request(f"{VIGILANT_URL}{endpoint}")
    req.add_header("Host", domain)
    
    start_time = time.time()
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            status = response.getcode()
            body = response.read().decode('utf-8')[:50]
    except urllib.error.HTTPError as e:
        status = e.code
        body = e.reason
    except Exception as e:
        status = 0
        body = str(e)
    
    elapsed = time.time() - start_time
    
    if verbosity >= 2:
        if status == 200:
            print(f"[SUCCESS] {domain} responded in {elapsed:.2f}s")
        else:
            print(f"[ERROR]   {domain} failed with {status}: {body} ({elapsed:.2f}s)")
            
    return status, elapsed

def traffic_burst(domain, count, threads, verbosity):
    print(f"\n--- Starting burst of {count} requests to {domain} using {threads} threads ---")
    
    results = []
    
    def worker(num_reqs):
        for _ in range(num_reqs):
            results.append(make_request(domain, verbosity=verbosity))
            
    # Divide work
    reqs_per_thread = count // threads
    remainder = count % threads
    
    thread_list = []
    for i in range(threads):
        worker_reqs = reqs_per_thread + (1 if i < remainder else 0)
        t = threading.Thread(target=worker, args=(worker_reqs,))
        thread_list.append(t)
        t.start()
        
    start_burst = time.time()
    
    for t in thread_list:
        t.join()
        
    total_time = time.time() - start_burst
    
    success = sum(1 for r in results if 200 <= r[0] < 400)
    failed = len(results) - success
    avg_ping = sum(r[1] for r in results) / len(results) if results else 0
    
    print(f"> Burst completed in {total_time:.2f}s")
    print(f"> Success: {success} | Failed: {failed} | Avg Latency: {avg_ping:.2f}s")
    print("-" * 50)

def simulate_low(verbosity):
    print("\n[SIMULATION: LOW TRAFFIC]")
    print("Simulating sparse user traffic. One request every 2 seconds to random services.")
    
    # 5 requests, 2s apart
    for idx in range(5):
        domain = DOMAINS[idx % len(DOMAINS)]
        make_request(domain, verbosity=2)  # Force logging
        time.sleep(2)

def simulate_normal(verbosity):
    print("\n[SIMULATION: NORMAL TRAFFIC]")
    print("Simulating steady operations. Batches of 20 requests hitting 2 services.")
    
    traffic_burst(DOMAINS[0], count=20, threads=2, verbosity=verbosity)
    time.sleep(1)
    traffic_burst(DOMAINS[1], count=20, threads=2, verbosity=verbosity)

def simulate_high(verbosity):
    print("\n[SIMULATION: HIGH TRAFFIC]")
    print("Simulating heavy load/DDoS. 500 requests per service rapidly firing.")
    
    for domain in DOMAINS:
        traffic_burst(domain, count=500, threads=20, verbosity=verbosity)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Vigilant Traffic Simulator")
    parser.add_argument("--mode", choices=["low", "normal", "high", "all"], default="all",
                        help="Traffic mode to simulate")
    parser.add_argument("-v", "--verbose", action="count", default=1,
                        help="Increase output verbosity")
    
    args = parser.parse_args()
    
    if args.mode in ["low", "all"]:
        simulate_low(args.verbose)
    if args.mode in ["normal", "all"]:
        simulate_normal(args.verbose)
    if args.mode in ["high", "all"]:
        simulate_high(args.verbose)

    print("\nSimulation complete. Check Vigilant logs to verify automatic sleep/wake behavior!")
