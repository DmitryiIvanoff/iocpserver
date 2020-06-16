
<?php
# Fill our vars and run on cli
# $ php -f db-connect-test.php

$dbname = 'test';
$dbuser = 'root';
$dbpass = '';
$dbhost = '127.0.0.1:4000';

$link = mysqli_connect($dbhost, $dbuser, $dbpass) or die("Unable to Connect to '$dbhost'");
mysqli_select_db($link, $dbname) or die("Could not open the db '$dbname'");
for( $i = 0; $i < 10000 ; $i++){
	$test_query = "SHOW TABLES FROM $dbname";
	$result = mysqli_query($link, $test_query);

	$tblCnt = 0;
	while($tbl = mysqli_fetch_array($result)) {
	  $tblCnt++;
	  #echo $tbl[0]."<br />\n";
	}

	if (!$tblCnt) {
	  echo "There are no tables<br />\n";
	} else {
	  echo "There are $tblCnt tables<br />\n";
	}
}
mysqli_close($link);

?>