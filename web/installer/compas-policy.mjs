/* SPDX-License-Identifier: GPL-3.0-only */
import { Package, logicalImage, productOf, sha256hex, validateStockPackage } from "./fm1pkg.js";

export const COMPAS_PRODUCT = "FM-1_92210003";
export const STARTING_PRODUCTS = Object.freeze([
  "FM-1_015", "FM-1_92200105", "FM-1_92200200", "FM-1_92200201", COMPAS_PRODUCT,
]);

export async function validateCompasPackage(bytes, expectedSHA256) {
  if (!/^[0-9a-f]{64}$/.test(expectedSHA256))
    throw new Error("A pinned Compás package hash is required.");
  if (productOf(bytes) !== COMPAS_PRODUCT)
    throw new Error("The package identity differs from this Compás release.");
  if (await sha256hex(bytes) !== expectedSHA256)
    throw new Error("The Compás package checksum differs.");
  const pkg = new Package(bytes);
  if (pkg.appBase !== 0x4000 || pkg.appOff !== 0x4120 ||
      pkg.app.size !== 0x8AFBC || pkg.flashSize !== 0x90000)
    throw new Error("The package does not preserve the Compás show area.");
  return { product: pkg.product, image: logicalImage(bytes) };
}

export async function validateOfficialReturn(bytes) {
  const stock = await validateStockPackage(bytes);
  if (stock.product !== "FM-1_015")
    throw new Error("This Compás return route is qualified only against the V15 fixture.");
  return stock;
}
