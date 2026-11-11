# ISE Project — Barcode Generator and Reader

A Windows desktop app built with C++ and Qt Widgets. It generates Code 39 barcodes from six-digit numbers, saves them as PNGs, and reads the original number from a saved barcode image.

## Build and run

1. Install Visual Studio with Desktop development with C++ and Qt Visual Studio Tools.
2. Install Qt 6.12.0 for MSVC 2022 64-bit.
3. Open `ISEBarcodeProject.slnx` and select `Debug | x64`.
4. Build the solution and press F5 to run it.

## Quick demo

1. Enter `123456` and click **Generate**. The first digit must be between 1 and 9.
2. Click **Save PNG** and choose a location.
3. Click **Read PNG** and open the saved file. The app should display `123456`.

## Team

Farhad, Anaya, Shaffay, Maryam, and Shahzeen

## Team contributions

Member: Contribution 

 - Farhad   : Built the Qt interface and connected the Generate, Save PNG, and Read PNG workflows. 
 - Anaya    : Implemented the shared Code 39 pattern table and barcode generator. 
 - Maryam   : Implemented image processing to extract bar and space widths from the barcode image. 
 - Shaffay  : Implemented the Code 39 decoder to recover the original six digits. 
 - Shahzeen : Implemented six-digit input validation and PNG saving and loading. 
