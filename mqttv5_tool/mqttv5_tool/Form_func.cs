using System;
using System.Collections.Generic;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace mqttv5_tool
{
    static class Form_func
    {
        static public void init(DataGridView dataGridView)
        {
            //只允许选中单行
            dataGridView.MultiSelect = false;
            //整行选中
            dataGridView.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            dataGridView.ReadOnly = true;
            //禁止排序
            //for (int i = 0; i < dataGridView.Columns.Count; i++)
            //{
            //    dataGridView.Columns[i].SortMode = DataGridViewColumnSortMode.NotSortable;
            //}
            //不显示左侧空白行
            dataGridView.RowHeadersVisible = false;
            //禁止用户改变DataGridView的所有列的列宽   
            dataGridView.AllowUserToResizeColumns = false;
            //禁止用户改变DataGridView所有行的行高   
            dataGridView.AllowUserToResizeRows = false;
            //文本居中
            dataGridView.RowsDefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleCenter;

            dataGridView.ColumnHeadersDefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleCenter;

            dataGridView.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCellsExceptHeader;
            //默认选中第一行
            //dataGridView.CurrentCell = dataGridView.Rows[0].Cells[1];
            //dataGridView.Rows.Clear();

        }
    }
}
