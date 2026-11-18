# ISE Project — Barcode Generator and Reader

A Windows desktop app built with C++ and Qt Widgets. It generates Code 39 barcodes from six-digit numbers, saves them as PNGs, and reads barcodes from a saved image or a live webcam.

## Build and run

1. Install Visual Studio with the **Desktop development with C++** workload and the **Qt Visual Studio Tools** extension.
2. Install **Qt 6.12.0 for MSVC 2022 64-bit** with the **Qt Multimedia** component.
3. Open `ISEBarcodeProject.slnx`. In the project's Qt settings, select the `6.12.0_msvc2022_64` kit and check that **Multimedia** and **Multimedia Widgets** are enabled.
4. Select **Debug | x64**, build the solution, and press **F5**.

## Quick demo

1. Enter `123456` and click **Generate**. The app accepts six digits starting with `1`–`9`.
2. Click **Save PNG** and choose a location.
3. Click **Read PNG** and open the saved file. The decoded number should be `123456`.
4. To test the webcam, display that PNG on a phone or print it. Click **Scan Camera** and hold the barcode horizontally in the center of the preview with both white margins visible.
5. When the app reads the barcode, it displays `123456` and closes the preview. Click **Cancel** to stop scanning at any time.

## Team contributions

Team Member: Contribution
- Farhad   : Built the Qt interface and connected the Generate, Save PNG, Read PNG, and Scan Camera workflows.
- Anaya    : Implemented the shared Code 39 pattern table and barcode generator.
- Maryam   : Implemented bar and space extraction for saved images and live camera frames.
- Shaffay  : Implemented the Code 39 decoder and added relative-width matching for camera scans.
- Shahzeen : Implemented six-digit input validation, PNG saving and loading, and live webcam capture.
