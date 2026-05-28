"""
Plot für MAX30100-Rohwerte aus einer CSV-Datei.

Erwartetes CSV-Format zum Beispiel:
    time,IR,R
    0.000,52340,48120
    0.010,52355,48100

Oder:
    Zeit;IR;RED
    0;52340;48120
    10;52355;48100

Aufruf im Terminal:
    python plot_ir_red_csv.py messung.csv

Optional:
    python plot_ir_red_csv.py messung.csv --save plot.png
"""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def find_column(columns, possible_names):
    """Sucht eine Spalte anhand typischer Namen, unabhängig von Groß-/Kleinschreibung."""
    normalized = {str(col).strip().lower(): col for col in columns}

    for name in possible_names:
        key = name.strip().lower()
        if key in normalized:
            return normalized[key]

    # Fallback: enthält Name als Teilstring, z. B. "ir_raw" oder "time_ms"
    for col in columns:
        col_lower = str(col).strip().lower()
        for name in possible_names:
            if name.strip().lower() in col_lower:
                return col

    return None


def load_csv(csv_path):
    """Lädt CSV robust mit automatischer Trennzeichenerkennung."""
    try:
        df = pd.read_csv(csv_path, sep=None, engine="python")
    except Exception:
        # Fallback für einfache komma-getrennte Dateien ohne automatische Erkennung
        df = pd.read_csv(csv_path)

    # Leere Spalten entfernen, falls z. B. durch ein Semikolon am Zeilenende entstanden
    df = df.dropna(axis=1, how="all")
    return df


def main():
    parser = argparse.ArgumentParser(description="IR- und R/RED-Rohwerte aus CSV über der Zeit plotten.")
    parser.add_argument("csv_file", help="Pfad zur CSV-Datei")
    parser.add_argument("--save", default="ir_red_plot.png", help="Dateiname für gespeicherten Plot, Standard: ir_red_plot.png")
    parser.add_argument("--show", action="store_true", help="Plot zusätzlich anzeigen")
    args = parser.parse_args()

    csv_path = Path(args.csv_file)
    if not csv_path.exists():
        raise FileNotFoundError(f"CSV-Datei nicht gefunden: {csv_path}")

    df = load_csv(csv_path)

    time_col = find_column(df.columns, ["time", "zeit", "t", "ms", "millis", "timestamp"])
    ir_col = find_column(df.columns, ["ir", "IR"])
    red_col = find_column(df.columns, ["r", "red", "RED", "rot"])

    # Falls keine Zeitspalte gefunden wird: erste Spalte als Zeit verwenden
    if time_col is None:
        time_col = df.columns[0]

    # Falls IR/R nicht über Namen gefunden werden: zweite und dritte Spalte verwenden
    if ir_col is None and len(df.columns) >= 2:
        ir_col = df.columns[1]
    if red_col is None and len(df.columns) >= 3:
        red_col = df.columns[2]

    if ir_col is None or red_col is None:
        raise ValueError(
            "Konnte IR- und R/RED-Spalten nicht sicher finden. "
            f"Gefundene Spalten: {list(df.columns)}"
        )

    # Werte numerisch machen; ungültige Zeilen werden entfernt
    plot_df = df[[time_col, ir_col, red_col]].copy()
    plot_df.columns = ["time", "IR", "R/RED"]

    for col in plot_df.columns:
        plot_df[col] = pd.to_numeric(plot_df[col], errors="coerce")

    plot_df = plot_df.dropna()

    if plot_df.empty:
        raise ValueError("Nach dem Einlesen bleiben keine numerischen Messwerte übrig.")

    plt.figure(figsize=(12, 6))
    plt.plot(plot_df["time"], plot_df["IR"], label="IR-Rohwert")
    plt.plot(plot_df["time"], plot_df["R/RED"], label="R/RED-Rohwert")

    plt.xlabel(f"Zeit ({time_col})")
    plt.ylabel("Rohwert")
    plt.title("MAX30100 Rohwerte über der Zeit")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    output_path = Path(args.save)
    plt.savefig(output_path, dpi=200)
    print(f"Plot gespeichert als: {output_path.resolve()}")
    print(f"Verwendete Spalten: Zeit='{time_col}', IR='{ir_col}', R/RED='{red_col}'")

    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
