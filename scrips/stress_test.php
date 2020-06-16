<?php
$sStart = microtime(true);

function random_str(){

$result="";
for ($i = 1; $i <= 16; $i++) {
$base10Rand = mt_rand(0, 15);
$newRand = base_convert($base10Rand, 10, 36);
$result.=$newRand;
}
return $result;

}

$dbname = 'test';
$dbuser = 'root';
$dbpass = '';
$dbhost = '127.0.0.1:4000';

$link = mysqli_connect($dbhost, $dbuser, $dbpass) or die("Unable to Connect to '$dbhost'");
mysqli_select_db($link, $dbname) or die("Could not open the db '$dbname'");

echo number_format(microtime(true) - $sStart, 2) ."s connect done\n";flush();

$tbl_number=rand(1000, 9999);

$query = "CREATE TABLE IF NOT EXISTS test_tbl_$tbl_number (test1 text(100) NOT NULL, test2 text(100) NOT NULL, test3 text(100) NOT NULL, test4 text(100) NOT NULL) ENGINE=MyISAM DEFAULT CHARSET=latin1;";
mysqli_query($link, $query) || die (print 'no query');
echo number_format(microtime(true) - $sStart, 2) ."s CREATE done\n";flush();
$aa=array();
for ($i=1; $i <= 10000; $i++) {
$a=random_str();
$aa[]=$a;
$b=random_str();
$c=random_str();
$d=random_str();
$query="INSERT INTO test_tbl_$tbl_number SET test1='$a', test2='$b', test3='$c', 
test4='$d'";
mysqli_query($link, $query) || die (print 'no insert:'.mysqli_error($link));
}
echo number_format(microtime(true) - $sStart, 2) ."s INSERT (1.000) 
done\n";flush();

$query="SELECT SQL_NO_CACHE * FROM test_tbl_$tbl_number";
mysqli_query($link, $query) || die (print 'no select_db 2');
echo number_format(microtime(true) - $sStart, 2) ."s SELECT (1) done\n";flush();

 foreach ($aa as $value) {
$query="DELETE FROM test_tbl_$tbl_number WHERE test1='$value'";
 mysqli_query($link, $query) || die (print 'no delete');
 }
echo number_format(microtime(true) - $sStart, 2) ."s DELETE (1.000) 
done\n";flush();

$query="DROP TABLE test_tbl_$tbl_number";
mysqli_query($link, $query) || die (print 'no drop');
echo number_format(microtime(true) - $sStart, 2) ."s DROP (1.000) done\n";flush();

?>