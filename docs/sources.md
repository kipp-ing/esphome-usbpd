# Sources

The knowledge base (`docs/knowledge/`) and the spec (`docs/spec.md`) cite sections, tables and line
numbers from the documents below. The documents themselves are not included in this repository;
get them from the publisher.

## USB Power Delivery

- USB Power Delivery specification, document library at USB-IF:
  https://www.usb.org/document-library/usb-power-delivery
  - Revision 2.0, Version 1.3: used for the on-wire protocol (SpecRev 2.0 is what the firmware sends)
  - Revision 3.2, Version 1.2: used for cross-checks and newer timer values
- USB Type-C Cable and Connector specification (sink attach, Rd, vRd levels):
  https://www.usb.org/document-library/usb-type-cable-and-connector-specification

Line references like `usb-pd-spec-r2.0-v1.3.txt:1234` point into a plain-text export of these PDFs
(`pdftotext`). To follow them, export the PDF of the same revision yourself.

## FUSB302

- onsemi FUSB302 product page and datasheet (FUSB302/D, July 2017 revision used):
  https://www.onsemi.com/products/interfaces/usb-type-c/fusb302
- References like `fusb302mpx.txt:NNN` point into a `pdftotext` export of that datasheet.

## Reference driver

- Linux kernel TCPM driver for the FUSB302, used for the exact init and RX sequence:
  https://github.com/torvalds/linux/blob/55311a92bc9564b058a036074f85200a5954ccd2/drivers/usb/typec/tcpm/fusb302.c
  and `fusb302_reg.h` in the same directory.

## Private material (not published)

The build day worked from local copies of the above documents and from a private mission-control
repository (earlier attempts, rig scripts, retrospectives). Those are not published; the parts that
matter are summarised in [`how-we-built-it.md`](how-we-built-it.md).
