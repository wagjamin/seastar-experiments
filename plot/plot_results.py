#!/usr/bin/env python3
import duckdb
import matplotlib.pyplot as plt
from pathlib import Path

def load_and_aggregate(filename):
    """Load CSV and aggregate metrics across all shards."""
    try:
        # Connect to DuckDB and read CSV
        con = duckdb.connect()
        df = con.execute(f"""
            SELECT 
                date_trunc('second', epoch_ms(timestamp)) as time,
                SUM(gbits) as total_gbits,
                SUM(pps) / 1000 as total_pps
            FROM read_csv_auto('{filename}')
            GROUP BY time
            ORDER BY time
        """).df()
        return df
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        
def create_plot(df, title_prefix, output_file):
    """Create throughput and PPS plots for a single dataset."""
    if df is None:
        return
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
    
    # Plot Gbits
    ax1.plot(df['time'], df['total_gbits'], color='blue')
    ax1.set_title(f'{title_prefix} Network Throughput Over Time')
    ax1.set_xlabel('Time')
    ax1.set_ylabel('Throughput (Gbits)')
    ax1.grid(True)

    # Plot PPS
    ax2.plot(df['time'], df['total_pps'], color='blue')
    ax2.set_title(f'{title_prefix} Packets Per Second Over Time')
    ax2.set_xlabel('Time')
    ax2.set_ylabel('kPPS')
    ax2.grid(True)

    plt.tight_layout()
    plt.savefig(output_file, format='pdf')
    plt.close()
    print(f"Plot saved as {output_file}")

def main():
    # Create output directory if it doesn't exist
    output_dir = Path('plot')
    output_dir.mkdir(exist_ok=True)

    # Load data
    server_df = load_and_aggregate('server_report.csv')
    client_df = load_and_aggregate('client_report.csv')

    if server_df is None and client_df is None:
        print("No data files found!")
        return

    # Create separate plots
    if server_df is not None:
        create_plot(server_df, "Server", output_dir / "server_results.pdf")
    if client_df is not None:
        create_plot(client_df, "Client", output_dir / "client_results.pdf")

if __name__ == "__main__":
    main() 
