DATASET_PATH=/home/heyuhan/other/m2bench/Datasets
USERNAME=root

## ecommerce #######################################################################################################

## LOAD TABLE

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/table/Brand.csv" --type csv --collection "Brand" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/table/Customer.csv" --type csv --separator "|" --collection "Customer" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/table/Product.csv" --type csv --collection "Product" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce



# ## LOAD JSON


# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/json/order.json" --type json --collection "Order" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/json/review.json" --type json --collection "Review" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce


# ## LOAD GRAPH

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/property_graph/person_node.csv" --separator "|" --type csv  --translate "person_id=_key" --collection "Person" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/property_graph/hashtag_node_SF1.csv" --type csv  --translate "tag_id=_key" --collection "Shopping_Hashtag" --server.username $USERNAME   --create-collection true --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/property_graph/person_follows_person.csv" --type csv --separator "|" --translate "_from=_from" --translate "_to=_to" --collection "Follows" --from-collection-prefix Person --to-collection-prefix Person --server.username $USERNAME   --create-collection true --create-collection-type edge --threads 16 --server.database Ecommerce

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/ecommerce/property_graph/person_interestedIn_tag.csv" --type csv --translate "person_id=_from" --translate "tag_id=_to" --translate "created_time=created_date" --collection "Interested_in" --from-collection-prefix Person --to-collection-prefix Shopping_Hashtag --server.username $USERNAME   --create-collection true --create-collection-type edge --threads 16 --server.database Ecommerce

# arangosh --server.database "Ecommerce" --server.username $USERNAME  --javascript.execute  $DATASET_PATH/../Impl/arangodb/load_datasets/ecommerce/create_index.js

## healthcare #######################################################################################################

# LOAD TABLE

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/table/Diagnosis.csv" --type csv --collection "Diagnosis" --server.username $USERNAME   --create-collection true --threads 16 --server.database Healthcare

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/table/Patient.csv" --type csv --collection "Patient" --server.username $USERNAME   --create-collection true --threads 16 --server.database Healthcare

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/table/Prescription.csv" --type csv --collection "Prescription" --server.username $USERNAME   --create-collection true --threads 16 --server.database Healthcare


# LOAD JSON

./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/json/drug.json" --batch-size 536870912 --type json --collection "Drug" --server.username $USERNAME   --create-collection true --threads 16 --server.database Healthcare


# LOAD GRAPH


# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/property_graph/Disease_network_nodes.csv" --type csv --translate "disease_id=_key" --collection "Disease_network_nodes" --server.username $USERNAME   --create-collection true --threads 16 --server.database Healthcare

# ./build-presets/mybuild/bin/arangoimport -c ./etc/relative/arangoimport.conf --overwrite true --file "$DATASET_PATH/healthcare/property_graph/Disease_network_edges.csv" --type csv --translate "source_id=_from" --translate "destination_id=_to" --collection "Disease_network_edges" --from-collection-prefix Disease_network_nodes --to-collection-prefix Disease_network_nodes --server.username $USERNAME   --create-collection true --create-collection-type edge --threads 16 --server.database Healthcare

arangosh --server.database Healthcare --server.username $USERNAME  --javascript.execute  $DATASET_PATH/../Impl/arangodb/load_datasets/healthcare/create_index.js

