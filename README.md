# Super Duper Ultra Mega Store

A Windows desktop point-of-sale application built by RandomKidz for the ISE term project. The app uses a webcam to scan six-digit Code 39 barcodes, look up products, record orders, and generate itemized bills.

## Project increments

- **Increment 1** introduced barcode generation, PNG saving and reading, and webcam scanning. The original version is available under the [`Increment-1` tag](https://github.com/FarhadGoated/ISE-Project/tree/Increment-1).
- **Increment 2** reuses the webcam scanner and Code 39 decoder for product lookup and checkout. The POS adds quantity and stock checks, bill calculation, and linked order records.
## Software engineering practices

- **Incremental development** — We built and tested barcode generation and reading in Increment 1. Increment 2 uses the same scanner and decoder to look up products, record orders, and generate bills. The `Increment-1` tag preserves the earlier version for comparison.
- **Separation of concerns** — The Qt interface handles user interaction. `ProductCatalog` loads products, `Order` validates quantities and stock, `BillCalculator` calculates totals, and `CheckoutStorage` saves the records.
- **Abstraction and low coupling** — The GUI calls shared module interfaces. For example, it can look up a product by barcode without knowing how `Product.txt` is parsed.
- **Reuse and simplicity** — Product Lookup and Scan Product share one camera workflow. The bill screen and file writer use the same calculated bill, so totals are calculated in one place.
- **Validation and error handling** — The app validates barcode input and stock limits, reports file errors, and keeps the cart open when a checkout save fails.
- **Traceability** — `Order.txt`, `OrderDetail.txt`, and `Bill.txt` share an Order ID, linking each bill to its order and items.
- **Version control and documentation** — Feature branches and pull requests track team contributions. The README and implementation guides document setup, module interfaces, and the demo workflow.
  
## Build and run

1. Install Visual Studio with the **Desktop development with C++** workload and the **Qt Visual Studio Tools** extension.
2. Install **Qt 6.12.0 for MSVC 2022 64-bit** with the **Qt Multimedia** component.
3. Open `ISEBarcodeProject.slnx`. In the project’s Qt settings, select the `6.12.0_msvc2022_64` kit and check that **Multimedia** and **Multimedia Widgets** are enabled.
4. Select **Debug | x64**, build the solution, and press **F5**. A webcam is required for live scanning.

## Quick demo

1. Open `data/barcodes/123456.png` on a phone or print it with both white margins visible.
2. Click **Product Lookup** and hold the barcode horizontally in the camera preview. The app should display **Whole Milk**.
3. Open **New Order → Scan Product**, scan the barcode again, and enter a quantity of `1`. Select the item and click **Description** to review it.
4. Click **Generate Bill**, enter the customer name, choose **Cash**, and click **Save Bill**. The app displays the itemized bill and writes the checkout records to `data`.

The catalog requires the requested quantity to be below the available stock. Successful checkout deducts the sold quantity.

## Data files

| File | Contents |
|---|---|
| `data/Product.txt` | Product IDs, barcodes, descriptions, prices, tax, discounts, stock, and barcode image paths |
| `data/Order.txt` | Customer and payment information for each saved order |
| `data/OrderDetail.txt` | Products and quantities associated with each order |
| `data/Bill.txt` | Itemized billing data, totals, and payment status |

The three checkout files are created on the first successful sale and share the same Order ID for that sale.

## Software engineering principles

- **Incremental development** — Increment 1 delivered barcode generation and reading. Increment 2 built on that foundation with product lookup, order entry, billing, and stock updates. The `Increment-1` tag preserves the first version for comparison.
- **Separation of concerns** — `MainWindow` handles user interaction, `ProductCatalog` loads product data, `Order` checks quantities,

## Team contributions

- **Farhad** — Built the Qt interface and integrated webcam lookup, order entry, and bill display.
- **Anaya** — Implemented the Code 39 generator and the product catalog parser and lookup.
- **Maryam** — Implemented barcode image processing and the order quantity and stock checks.
- **Shaffay** — Implemented Code 39 decoding and the bill calculator.
- **Shahzeen** — Implemented input validation, PNG file handling, webcam capture, and checkout storage.

## Current scope

The checkout GUI saves completed sales as `paid`. Hold, resume, and archive actions are pending. Use Cash or Card for the demo while the Wallet option is being aligned with checkout storage.
