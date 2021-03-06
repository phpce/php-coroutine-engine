--TEST--
Test scandir() function : usage variations - different ints as $sorting_order arg
--SKIPIF--
<?php
if (substr(PHP_OS, 0, 3) != 'WIN') {
  die("skip Valid only on Windows");
}
?>
--FILE--
<?php
/* Prototype  : array scandir(string $dir [, int $sorting_order [, resource $context]])
 * Description: List files & directories inside the specified path 
 * Source code: ext/standard/dir.c
 */

/*
 * Pass different integers as $sorting_order argument to test how scandir()
 * re-orders the array
 */

echo "*** Testing scandir() : usage variations ***\n";

// include for create_files/delete_files functions
include(dirname(__FILE__) . '/../file/file.inc');

// create directory and files
$dir = dirname(__FILE__) . '/私はガラスを食べられますscandir_variation9';
mkdir($dir);
@create_files($dir, 2, "numeric", 0755, 1, "w", "私はガラスを食べられますfile");

// different ints to pass as $sorting_order argument
$ints = array (PHP_INT_MAX, -PHP_INT_MAX, 0);

foreach($ints as $sorting_order) {
	var_dump( scandir($dir, $sorting_order) );
}

delete_files($dir, 2, "私はガラスを食べられますfile");
?>
===DONE===
--CLEAN--
<?php
$dir = dirname(__FILE__) . '/私はガラスを食べられますscandir_variation9';
rmdir($dir);
?>
--EXPECTF--
*** Testing scandir() : usage variations ***
array(4) {
  [0]=>
  string(45) "私はガラスを食べられますfile2.tmp"
  [1]=>
  string(45) "私はガラスを食べられますfile1.tmp"
  [2]=>
  string(2) ".."
  [3]=>
  string(1) "."
}
array(4) {
  [0]=>
  string(45) "私はガラスを食べられますfile2.tmp"
  [1]=>
  string(45) "私はガラスを食べられますfile1.tmp"
  [2]=>
  string(2) ".."
  [3]=>
  string(1) "."
}
array(4) {
  [0]=>
  string(1) "."
  [1]=>
  string(2) ".."
  [2]=>
  string(45) "私はガラスを食べられますfile1.tmp"
  [3]=>
  string(45) "私はガラスを食べられますfile2.tmp"
}
===DONE===
